<script lang="ts">
  // S8 spike: WebGL availability and fps in the welcome window. Replaced by the OOTB flow.
  import { onMount } from "svelte";
  import * as THREE from "three";
  import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";

  let canvas: HTMLCanvasElement;
  let info = $state("starting");

  onMount(() => {
    let r: THREE.WebGLRenderer;
    try {
      r = new THREE.WebGLRenderer({ canvas, antialias: true });
    } catch (e) {
      info = `no WebGL: ${e}`;
      return;
    }
    const gl = r.getContext();
    const dbg = gl.getExtension("WEBGL_debug_renderer_info");
    const gpu = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
    r.setPixelRatio(window.devicePixelRatio);
    r.toneMapping = THREE.ACESFilmicToneMapping;
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x000000);
    const pm = new THREE.PMREMGenerator(r);
    scene.environment = pm.fromScene(new RoomEnvironment(), 0.04).texture;
    const cam = new THREE.PerspectiveCamera(30, 1, 1, 1000);
    cam.position.set(0, 60, 220);
    cam.lookAt(0, 0, 0);
    const pcb = new THREE.Mesh(
      new THREE.BoxGeometry(86, 1.6, 50),
      new THREE.MeshPhysicalMaterial({ color: 0x1a1a1c, roughness: 0.5, clearcoat: 0.6 }),
    );
    const glass = new THREE.Mesh(
      new THREE.BoxGeometry(70, 3, 50),
      new THREE.MeshPhysicalMaterial({ color: 0x050506, roughness: 0.05, metalness: 0, clearcoat: 1 }),
    );
    glass.position.y = 2.3;
    const board = new THREE.Group();
    board.add(pcb, glass);
    scene.add(board);
    const key = new THREE.DirectionalLight(0xffffff, 2);
    key.position.set(50, 100, 80);
    scene.add(key);

    let frames = 0;
    let t0 = performance.now();
    let raf = 0;
    const size = () => {
      r.setSize(window.innerWidth, window.innerHeight, false);
      cam.aspect = window.innerWidth / window.innerHeight;
      cam.updateProjectionMatrix();
    };
    size();
    window.addEventListener("resize", size);
    const loop = (t: number) => {
      board.rotation.y = t / 3000;
      r.render(scene, cam);
      frames++;
      if (t - t0 > 2000) {
        info = `${gpu} · ${((frames * 1000) / (t - t0)).toFixed(0)} fps · ${window.innerWidth}×${window.innerHeight} @${window.devicePixelRatio}`;
        frames = 0;
        t0 = t;
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  });
</script>

<canvas bind:this={canvas}></canvas>
<p class="info">{info}</p>

<style>
  canvas {
    position: fixed;
    inset: 0;
    width: 100%;
    height: 100%;
  }
  .info {
    position: fixed;
    left: 16px;
    bottom: 16px;
    font-size: 15px;
    color: #fff;
  }
</style>
