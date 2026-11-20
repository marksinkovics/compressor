"""Installs the built wheel with pip into a new venv and uses it, run by ctest (Python.Wheel).

    python3 -I python/test/install_wheel.py WHEEL_DIR WORK_DIR
"""

import pathlib
import shutil
import subprocess
import sys
import venv


def main():
    wheels = sorted(pathlib.Path(sys.argv[1]).glob("ingot-*.whl"))
    assert len(wheels) == 1, wheels
    work = pathlib.Path(sys.argv[2])
    shutil.rmtree(work, ignore_errors=True)
    venv.EnvBuilder(with_pip=True).create(work)
    python = work / "bin" / "python"
    subprocess.run(
        [python, "-m", "pip", "install", "--no-index", "--quiet", "--disable-pip-version-check",
         str(wheels[0])],
        check=True,
    )
    script = (
        "import ingot, sys; data = b'wheel ' * 1000; "
        "assert ingot.decompress(ingot.compress(data, level=9)) == data; "
        "print('imported', ingot.__file__)"
    )
    # Run from the venv, not the build tree, so the installed module is the one imported.
    subprocess.run([python, "-I", "-c", script], check=True, cwd=work)
    print("installed", wheels[0].name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
