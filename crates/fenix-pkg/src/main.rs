//! `fenix-pkg build <dir> [-o out.fxp]` compiles a package; `fenix-pkg inspect <file>` lists one.

use std::path::{Path, PathBuf};

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let code = match args.first().map(String::as_str) {
        Some("build") if args.len() >= 2 => build(&args[1..]),
        Some("inspect") if args.len() == 2 => inspect(Path::new(&args[1])),
        _ => {
            eprintln!("usage: fenix-pkg build <dir> [-o out.fxp] | inspect <file.fxp>");
            2
        }
    };
    std::process::exit(code);
}

fn build(args: &[String]) -> i32 {
    let dir = PathBuf::from(&args[0]);
    let out = match args.iter().position(|a| a == "-o") {
        Some(i) => match args.get(i + 1) {
            Some(o) => PathBuf::from(o),
            None => {
                eprintln!("-o needs a file name");
                return 2;
            }
        },
        None => dir.join("app.fxp"),
    };
    match fenix_pkg::build(&dir).and_then(|b| {
        std::fs::write(&out, &b).map_err(|e| e.to_string())?;
        Ok(b)
    }) {
        Ok(b) => {
            let p = fenix_pkg::parse(&b).expect("just built");
            println!(
                "{} {} v{}: {} B, {} elements, {} bindings -> {}",
                p.id(),
                p.name(),
                p.version(),
                b.len(),
                p.elems.len(),
                p.binds.len(),
                out.display()
            );
            0
        }
        Err(e) => {
            eprintln!("error: {e}");
            1
        }
    }
}

fn inspect(file: &Path) -> i32 {
    let b = match std::fs::read(file) {
        Ok(b) => b,
        Err(e) => {
            eprintln!("{e}");
            return 1;
        }
    };
    match fenix_pkg::parse(&b) {
        Ok(p) => {
            println!("{} \"{}\" v{} (min fenixOS {}.{}.{}), {} B", p.id(), p.name(), p.version(),
                p.min_fenixos.0, p.min_fenixos.1, p.min_fenixos.2, b.len());
            println!("tile colour #{:06X}, icons {} / {}", p.tile_color, p.icon_small, p.icon_large);
            println!("bindings: {:?}", p.bind_names());
            for (i, e) in p.elems.iter().enumerate() {
                let ev = p.event_name(i as u16).map(|n| format!(" event={n}")).unwrap_or_default();
                println!("  #{i} kind {} parent {} props {:?}{ev}", e.kind, e.parent as i32, e.props);
            }
            for (i, a) in p.assets.iter().enumerate() {
                println!("  asset {i}: format {} {}x{} {} B", a.format, a.w, a.h, a.data.len());
            }
            0
        }
        Err(e) => {
            eprintln!("error: {e}");
            1
        }
    }
}
