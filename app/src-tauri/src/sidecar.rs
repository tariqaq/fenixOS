//! Runs one app's PC half (docs/sidecars.md): two one-way named pipes, the process (in a job
//! object, or through UAC when the manifest says `elevated`), and a thread that owns both.
//! The pipes only accept the process fenixOS started: the client's PID is checked on connect.

use std::fs::File;
use std::os::windows::io::{AsRawHandle, FromRawHandle};
use std::os::windows::process::CommandExt;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, RecvTimeoutError, Sender};
use std::sync::{Arc, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

use fenix_sdk::wire::{self, Down, Up};
use windows_sys::Win32::Foundation::{
    CloseHandle, GetLastError, ERROR_CANCELLED, ERROR_PIPE_CONNECTED, HANDLE, INVALID_HANDLE_VALUE,
    WAIT_OBJECT_0,
};
use windows_sys::Win32::Security::Cryptography::{
    BCryptGenRandom, BCRYPT_USE_SYSTEM_PREFERRED_RNG,
};
use windows_sys::Win32::Storage::FileSystem::{
    FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_ACCESS_INBOUND, PIPE_ACCESS_OUTBOUND,
};
use windows_sys::Win32::System::JobObjects::{
    AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation,
    SetInformationJobObject, JOBOBJECT_EXTENDED_LIMIT_INFORMATION,
    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
};
use windows_sys::Win32::System::Pipes::{
    ConnectNamedPipe, CreateNamedPipeW, GetNamedPipeClientProcessId, PIPE_READMODE_BYTE,
    PIPE_REJECT_REMOTE_CLIENTS, PIPE_TYPE_BYTE, PIPE_WAIT,
};
use windows_sys::Win32::System::Threading::{
    GetExitCodeProcess, GetProcessId, TerminateProcess, WaitForSingleObject, CREATE_NO_WINDOW,
};
use windows_sys::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
use windows_sys::Win32::UI::WindowsAndMessaging::SW_HIDE;

const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const STOP_GRACE: Duration = Duration::from_secs(3);

/// What the sidecar thread reports to the engine.
#[derive(Debug)]
pub enum Note {
    /// Both pipes connected and `hello` + `settings` sent.
    Ready,
    Up(Up),
    /// The process ended (or never started). `requested`: fenixOS stopped it.
    /// `declined`: the user said no to the UAC prompt.
    Exited {
        why: String,
        requested: bool,
        declined: bool,
    },
}

pub struct Spec {
    pub id: String,
    pub exe: PathBuf,
    pub elevated: bool,
    pub hello: Down,
    pub settings: Down,
}

pub struct Sidecar {
    tx: Sender<Down>,
}

impl Sidecar {
    pub fn start(spec: Spec, notify: impl Fn(Note) + Send + 'static) -> Sidecar {
        let (tx, rx) = mpsc::channel::<Down>();
        let name = format!("fenix-app-{}", spec.id);
        let _ = thread::Builder::new().name(name).spawn(move || {
            let mut proc = None;
            let exit = match run(&spec, &rx, &notify, &mut proc) {
                Ok(requested) => Note::Exited {
                    why: proc.as_ref().map(Proc::exit_text).unwrap_or_default(),
                    requested,
                    declined: false,
                },
                Err(Fail { why, declined }) => {
                    if let Some(p) = proc.as_ref() {
                        p.kill();
                    }
                    Note::Exited {
                        why,
                        requested: false,
                        declined,
                    }
                }
            };
            notify(exit);
        });
        Sidecar { tx }
    }

    pub fn send(&self, m: Down) {
        let _ = self.tx.send(m);
    }

    /// Ask it to exit; it is ended after 3 s if it does not.
    pub fn stop(&self) {
        let _ = self.tx.send(Down::Stop);
    }
}

struct Fail {
    why: String,
    declined: bool,
}

fn fail(why: impl Into<String>) -> Fail {
    Fail {
        why: why.into(),
        declined: false,
    }
}

/// Ok(true) when stopped by fenixOS, Ok(false) when the process exited by itself.
fn run(
    spec: &Spec,
    rx: &mpsc::Receiver<Down>,
    notify: &impl Fn(Note),
    proc_out: &mut Option<Proc>,
) -> Result<bool, Fail> {
    let base = format!(r"\\.\pipe\fenixos-{}-{}", spec.id, nonce());
    let down = Pipe::create(&format!("{base}.down"), true)?;
    let up = Pipe::create(&format!("{base}.up"), false)?;
    *proc_out = Some(Proc::start(spec, &base)?);
    let proc = proc_out.as_ref().unwrap();

    // ConnectNamedPipe has no timeout: if the app never connects, connect to ourselves to free it.
    let connected = Arc::new(AtomicBool::new(false));
    {
        let (connected, base) = (connected.clone(), base.clone());
        thread::spawn(move || {
            let end = Instant::now() + CONNECT_TIMEOUT;
            while Instant::now() < end && !connected.load(Ordering::Relaxed) {
                thread::sleep(Duration::from_millis(100));
            }
            if !connected.load(Ordering::Relaxed) {
                let _ = File::open(format!("{base}.down"));
                let _ = std::fs::OpenOptions::new()
                    .write(true)
                    .open(format!("{base}.up"));
            }
        });
    }
    let r = down.accept(proc.pid).and_then(|_| up.accept(proc.pid));
    connected.store(true, Ordering::Relaxed);
    r?;
    let mut down = down.into_file();
    let mut up = up.into_file();
    for m in [&spec.hello, &spec.settings] {
        wire::write(&mut down, m).map_err(|e| fail(format!("pipe: {e}")))?;
    }
    notify(Note::Ready);

    // Reader: app → fenixOS. Ends when the app closes its end or exits.
    let (up_tx, up_rx) = mpsc::channel::<Up>();
    thread::spawn(move || {
        while let Ok(m) = wire::read::<Up>(&mut up) {
            if let Some(m) = m {
                if up_tx.send(m).is_err() {
                    break;
                }
            }
        }
    });

    loop {
        while let Ok(m) = up_rx.try_recv() {
            notify(Note::Up(m));
        }
        if proc.exited() {
            while let Ok(m) = up_rx.try_recv() {
                notify(Note::Up(m));
            }
            return Ok(false);
        }
        match rx.recv_timeout(Duration::from_millis(100)) {
            Ok(Down::Stop) | Err(RecvTimeoutError::Disconnected) => {
                let _ = wire::write(&mut down, &Down::Stop);
                drop(down);
                let end = Instant::now() + STOP_GRACE;
                while Instant::now() < end && !proc.exited() {
                    thread::sleep(Duration::from_millis(50));
                }
                proc.kill();
                return Ok(true);
            }
            Ok(m) => {
                // A failed write means the app is going away; the exit check above reports it.
                let _ = wire::write(&mut down, &m);
            }
            Err(RecvTimeoutError::Timeout) => {}
        }
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(Some(0)).collect()
}

fn nonce() -> String {
    let mut b = [0u8; 16];
    unsafe {
        BCryptGenRandom(
            std::ptr::null_mut(),
            b.as_mut_ptr(),
            16,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG,
        )
    };
    crate::registry::hex(&b)
}

struct Pipe(HANDLE);

// The handle is used by one thread at a time.
unsafe impl Send for Pipe {}

impl Pipe {
    fn create(name: &str, outbound: bool) -> Result<Pipe, Fail> {
        let access = if outbound {
            PIPE_ACCESS_OUTBOUND
        } else {
            PIPE_ACCESS_INBOUND
        };
        let h = unsafe {
            CreateNamedPipeW(
                wide(name).as_ptr(),
                access | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1,
                64 * 1024,
                64 * 1024,
                0,
                std::ptr::null(),
            )
        };
        if h == INVALID_HANDLE_VALUE {
            return Err(fail(format!("pipe: error {}", unsafe { GetLastError() })));
        }
        Ok(Pipe(h))
    }

    /// Wait for the client and check it is the process we started.
    fn accept(&self, pid: u32) -> Result<(), Fail> {
        let ok = unsafe { ConnectNamedPipe(self.0, std::ptr::null_mut()) } != 0
            || unsafe { GetLastError() } == ERROR_PIPE_CONNECTED;
        if !ok {
            return Err(fail("pipe: connect failed"));
        }
        let mut client = 0u32;
        unsafe { GetNamedPipeClientProcessId(self.0, &mut client) };
        if client != pid {
            return Err(fail("did not connect to fenixOS in 10 s"));
        }
        Ok(())
    }

    fn into_file(self) -> File {
        let f = unsafe { File::from_raw_handle(self.0 as _) };
        std::mem::forget(self);
        f
    }
}

impl Drop for Pipe {
    fn drop(&mut self) {
        unsafe { CloseHandle(self.0) };
    }
}

struct Proc {
    handle: HANDLE,
    pid: u32,
    elevated: bool,
    // Keeps the std handle alive for non-elevated starts.
    _child: Option<std::process::Child>,
}

unsafe impl Send for Proc {}

impl Proc {
    fn start(spec: &Spec, base: &str) -> Result<Proc, Fail> {
        if !spec.elevated {
            let child = std::process::Command::new(&spec.exe)
                .arg("--fenix-pipe")
                .arg(base)
                .current_dir(spec.exe.parent().unwrap_or(&spec.exe))
                .creation_flags(CREATE_NO_WINDOW)
                .spawn()
                .map_err(|e| fail(format!("could not start: {e}")))?;
            let handle = child.as_raw_handle() as HANDLE;
            unsafe { AssignProcessToJobObject(job(), handle) };
            return Ok(Proc {
                handle,
                pid: child.id(),
                elevated: false,
                _child: Some(child),
            });
        }
        // UAC: Windows asks the user. This blocks only this sidecar's thread (PoC gotcha).
        let verb = wide("runas");
        let file = wide(&spec.exe.to_string_lossy());
        let params = wide(&format!("--fenix-pipe {base}"));
        let dir = wide(&spec.exe.parent().unwrap_or(&spec.exe).to_string_lossy());
        let mut info: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = params.as_ptr();
        info.lpDirectory = dir.as_ptr();
        info.nShow = SW_HIDE;
        if unsafe { ShellExecuteExW(&mut info) } == 0 || info.hProcess.is_null() {
            let err = unsafe { GetLastError() };
            return Err(Fail {
                why: if err == ERROR_CANCELLED {
                    "administrator rights declined".into()
                } else {
                    format!("could not start as administrator (error {err})")
                },
                declined: err == ERROR_CANCELLED,
            });
        }
        Ok(Proc {
            handle: info.hProcess,
            pid: unsafe { GetProcessId(info.hProcess) },
            elevated: true,
            _child: None,
        })
    }

    fn exited(&self) -> bool {
        unsafe { WaitForSingleObject(self.handle, 0) == WAIT_OBJECT_0 }
    }

    fn kill(&self) {
        if !self.exited() {
            // Fails for an elevated process; that one exits on its own when the pipe closes.
            unsafe { TerminateProcess(self.handle, 1) };
        }
    }

    fn exit_text(&self) -> String {
        let mut code = 0u32;
        unsafe { GetExitCodeProcess(self.handle, &mut code) };
        format!("exit code {code}")
    }
}

impl Drop for Proc {
    fn drop(&mut self) {
        if self.elevated {
            unsafe { CloseHandle(self.handle) };
        }
    }
}

/// One job for every non-elevated PC half: when fenixOS ends, Windows ends them too.
fn job() -> HANDLE {
    static JOB: OnceLock<usize> = OnceLock::new();
    *JOB.get_or_init(|| unsafe {
        let h = CreateJobObjectW(std::ptr::null(), std::ptr::null());
        let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(
            h,
            JobObjectExtendedLimitInformation,
            &info as *const _ as *const _,
            std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
        );
        h as usize
    }) as HANDLE
}
