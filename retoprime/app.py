from __future__ import annotations

import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from retoprime.core import RetopoJob, default_output_path
from retoprime.standalone_engine import inspect_mesh, retopologise


class RetoprimeApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("RETOPRIME")
        self.geometry("760x590")
        self.minsize(700, 540)
        self.configure(bg="#111722")

        self.input_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.faces_var = tk.IntVar(value=10_000)
        self.sharp_var = tk.BooleanVar(value=True)
        self.project_var = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="Ready — standalone engine")
        self.mesh_info_var = tk.StringVar(value="No mesh loaded")

        self._build_ui()

    def _build_ui(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#111722")
        style.configure("TLabel", background="#111722", foreground="#e9f2ff", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI", 28, "bold"), foreground="#42a5ff")
        style.configure("Section.TLabel", font=("Segoe UI", 11, "bold"), foreground="#cfe4ff")
        style.configure("TCheckbutton", background="#111722", foreground="#e9f2ff")

        frame = ttk.Frame(self, padding=28)
        frame.pack(fill="both", expand=True)

        ttk.Label(frame, text="RETOPRIME", style="Title.TLabel").pack(anchor="w")
        ttk.Label(frame, text="Standalone automatic mesh reduction — no Blender required").pack(anchor="w", pady=(0, 22))

        self._file_row(frame, "High-resolution model", self.input_var, self._choose_input)
        ttk.Label(frame, textvariable=self.mesh_info_var, foreground="#91a4ba").pack(anchor="w", pady=(4, 8))

        self._file_row(frame, "Output model", self.output_var, self._choose_output)

        ttk.Label(frame, text="Target face count", style="Section.TLabel").pack(anchor="w", pady=(18, 4))
        ttk.Spinbox(
            frame,
            from_=100,
            to=2_000_000,
            increment=1000,
            textvariable=self.faces_var,
        ).pack(fill="x")

        options = ttk.Frame(frame)
        options.pack(fill="x", pady=18)
        ttk.Checkbutton(options, text="Preserve hard features", variable=self.sharp_var).pack(side="left", padx=(0, 25))
        ttk.Checkbutton(options, text="Surface-aware reduction", variable=self.project_var).pack(side="left")

        note = (
            "Current standalone milestone: FBX/OBJ input + OBJ output + automatic topology-aware reduction. "
            "True quad-flow and native FBX export are the next milestones."
        )
        ttk.Label(frame, text=note, wraplength=690, foreground="#91a4ba").pack(anchor="w", pady=(0, 14))

        self.run_button = tk.Button(
            frame,
            text="RETOPOLOGISE",
            command=self._start,
            bg="#178bea",
            fg="white",
            activebackground="#42a5ff",
            relief="flat",
            font=("Segoe UI", 13, "bold"),
            pady=12,
        )
        self.run_button.pack(fill="x", pady=(6, 14))

        ttk.Label(frame, textvariable=self.status_var).pack(anchor="w")
        ttk.Label(
            frame,
            text="RETOPRIME v0.2 Standalone MVP",
            foreground="#65788e",
        ).pack(anchor="w", side="bottom")

    def _file_row(self, parent, title, variable, command):
        ttk.Label(parent, text=title).pack(anchor="w", pady=(8, 4))
        row = ttk.Frame(parent)
        row.pack(fill="x")
        ttk.Entry(row, textvariable=variable).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Browse", command=command).pack(side="left", padx=(8, 0))

    def _choose_input(self):
        selected = filedialog.askopenfilename(
            filetypes=[("3D models", "*.fbx *.obj"), ("FBX", "*.fbx"), ("OBJ", "*.obj"), ("All files", "*.*")]
        )
        if not selected:
            return

        path = Path(selected)
        self.input_var.set(str(path))
        self.output_var.set(str(default_output_path(path)))

        try:
            stats = inspect_mesh(path)
            self.mesh_info_var.set(f"{stats.vertices:,} vertices   •   {stats.faces:,} faces")
            if stats.faces > 0:
                suggested = max(100, min(100_000, int(stats.faces * 0.15)))
                self.faces_var.set(suggested)
        except Exception as exc:
            self.mesh_info_var.set(f"Could not inspect mesh: {exc}")

    def _choose_output(self):
        selected = filedialog.asksaveasfilename(
            defaultextension=".obj",
            filetypes=[("OBJ", "*.obj")],
        )
        if selected:
            self.output_var.set(selected)

    def _start(self):
        try:
            job = RetopoJob(
                Path(self.input_var.get()),
                Path(self.output_var.get()),
                self.faces_var.get(),
                self.sharp_var.get(),
                self.project_var.get(),
            )
            job.validate()
            if not job.input_path.is_file():
                raise ValueError("Choose an existing FBX or OBJ model.")
        except (ValueError, tk.TclError) as exc:
            messagebox.showerror("Check settings", str(exc))
            return

        self.run_button.configure(state="disabled")
        self.status_var.set("Retopologising…")
        threading.Thread(target=self._run_job, args=(job,), daemon=True).start()

    def _run_job(self, job: RetopoJob):
        try:
            stats = retopologise(
                job.input_path,
                job.output_path,
                job.target_faces,
                preserve_sharp=job.preserve_sharp,
                project_surface=job.project_surface,
            )
            self.after(0, self._finish_success, job, stats)
        except Exception as exc:
            self.after(0, self._finish_error, str(exc))

    def _finish_success(self, job: RetopoJob, stats):
        self.run_button.configure(state="normal")
        self.status_var.set(
            f"Complete — {stats.vertices:,} vertices / {stats.faces:,} faces — {job.output_path.name}"
        )
        messagebox.showinfo("RETOPRIME", f"Retopology complete.\n\nSaved to:\n{job.output_path}")

    def _finish_error(self, detail: str):
        self.run_button.configure(state="normal")
        self.status_var.set("Retopology failed")
        messagebox.showerror("Retopology failed", detail)


def main():
    RetoprimeApp().mainloop()


if __name__ == "__main__":
    main()
