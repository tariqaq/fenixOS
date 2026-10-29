import { mount } from "svelte";
import "./app.css";
import Welcome from "./ootb/Welcome.svelte";

export default mount(Welcome, { target: document.getElementById("app")! });
