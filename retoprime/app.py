from __future__ import annotations

import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from retoprime.core import RetopoJob, build_blender_command, default_output_path, find_blender


def resource_path(name: str) -> Path:
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return root / name


class RetoprimeApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("RETOPRIME")
        self.geometry("680x510")
        self.minsize(620, 470)
        self.configure(bg="#111722")
        self.input_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.faces_var = tk.IntVar(value=10_000)
        self.symmetry_var = tk.BooleanVar(value=False)
        self.sharp_var = tk.BooleanVar(value=True)
        self.project_var = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="Ready")
        self._build_ui()

    def _build_ui(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#111722")
        style.configure("TLabel", background="#111722", foreground="#e9f2ff", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI", 28, "bold"), foreground="#42a5ff")
        style.configure("TCheckbutton", background="#111722", foreground="#e9f2ff")
        frame = ttk.Frame(self, padding=28)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="RETOPRIME", style="Title.TLabel").pack(anchor="w")
        ttk.Label(frame, text="Automatic FBX / OBJ retopology powered by Blender QuadriFlow").pack(anchor="w", pady=(0, 25))
        self._file_row(frame, "High-resolution model", self.input_var, self._choose_input)
        self._file_row(frame, "Output model", self.output_var, self._choose_output)
        ttk.Label(frame, text="Target face count").pack(anchor="w", pady=(15, 4))
        ttk.Spinbox(frame, from_=100, to=2_000_000, increment=1000, textvariable=self.faces_var).pack(fill="x")
        options = ttk.Frame(frame)
        options.pack(fill="x", pady=18)
        ttk.Checkbutton(options, text="Symmetry", variable=self.symmetry_var).pack(side="left", padx=(0, 20))
        ttk.Checkbutton(options, text="Preserve sharp edges", variable=self.sharp_var).pack(side="left", padx=(0, 20))
        ttk.Checkbutton(options, text="Project to high-poly surface", variable=self.project_var).pack(side="left")
        self.run_button = tk.Button(frame, text="RETOPOLOGISE", command=self._start, bg="#178bea", fg="white", activebackground="#42a5ff", relief="flat", font=("Segoe UI", 13, "bold"), pady=12)
        self.run_button.pack(fill="x", pady=(8, 14))
        ttk.Label(frame, textvariable=self.status_var).pack(anchor="w")
        ttk.Label(frame, text="Requires Blender 4.0 or newer installed on this PC.", foreground="#91a4ba").pack(anchor="w", side="bottom")

    def _file_row(self, parent, title, variable, command):
        ttk.Label(parent, text=title).pack(anchor="w", pady=(8, 4))
        row = ttk.Frame(parent)
        row.pack(fill="x")
        ttk.Entry(row, textvariable=variable).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Browse", command=command).pack(side="left", padx=(8, 0))

    def _choose_input(self):
        selected = filedialog.askopenfilename(filetypes=[("3D models", "*.fbx *.obj"), ("All files", "*.*")])
        if selected:
            path = Path(selected)
            self.input_var.set(str(path))
            self.output_var.set(str(default_output_path(path)))

    def _choose_output(self):
        selected = filedialog.asksaveasfilename(defaultextension=".fbx", filetypes=[("FBX", "*.fbx"), ("OBJ", "*.obj")])
        if selected:
            self.output_var.set(selected)

    def _start(self):
        blender = find_blender()
        if blender is None:
            messagebox.showerror("Blender not found", "Install Blender 4.0 or newer, then reopen RETOPRIME.")
            return
        try:
            job = RetopoJob(Path(self.input_var.get()), Path(self.output_var.get()), self.faces_var.get(), self.symmetry_var.get(), self.sharp_var.get(), self.project_var.get())
            job.validate()
            if not job.input_path.is_file():
                raise ValueError("Choose an existing FBX or OBJ model.")
        except (ValueError, tk.TclError) as exc:
            messagebox.showerror("Check settings", str(exc))
            return
        self.run_button.configure(state="disabled")
        self.status_var.set("Retopologising… this may take several minutes.")
        threading.Thread(target=self._run_job, args=(blender, job), daemon=True).start()

    def _run_job(self, blender: Path, job: RetopoJob):
        worker = resource_path("blender_worker.py")
        result = subprocess.run(build_blender_command(blender, worker, job), capture_output=True, text=True)
        self.after(0, self._finish, result, job)

    def _finish(self, result, job):
        self.run_button.configure(state="normal")
        if result.returncode == 0 and job.output_path.is_file():
            self.status_var.set(f"Complete: {job.output_path}")
            messagebox.showinfo("RETOPRIME", "Retopology completed successfully.")
        else:
            self.status_var.set("Retopology failed")
            detail = (result.stderr or result.stdout or "Unknown Blender error")[-2500:]
            messagebox.showerror("Retopology failed", detail)


def main():
    RetoprimeApp().mainloop()


if __name__ == "__main__":
    main()
