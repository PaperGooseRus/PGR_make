# Third-party components · release checklist

PGR_Make is written in C++17. The optional graphical frontend uses GTK4 and the GLib/GTK dependency stack. Runtime bundles built from MSYS2 UCRT64 packages can include many other libraries and resources, and those packages carry their own licenses.

This file is **not** a substitute for the licenses shipped with third-party packages. Before publicly publishing a Windows package, collect, review, and distribute the required license texts and notices for the **actual versions shipped** in `Pack/GTK4`. The full dependency list varies by packaging environment. The first-party license for PGR_Make must also be selected by its maintainer before public distribution.

The CI job creates a **draft** release as a candidate, not an approved/public stable distribution. GTK4 build success and presence of DLLs do not by themselves prove that the runtime bundle runs on a clean Windows machine.
