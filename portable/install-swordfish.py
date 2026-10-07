#!/usr/bin/env python3

import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path, PurePosixPath


REPOSITORY = "hasmak-44/Swordfish-FM"
WORKFLOW = "portable-release.yml"
ARTIFACT_PREFIX = "Swordfish-Portable-"


class InstallError(Exception):
    pass


def run_command(command, *, capture_output=False):
    try:
        return subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=capture_output,
        )
    except OSError as error:
        raise InstallError(f"Could not run {command[0]}: {error}") from error


def ensure_github_cli():
    if shutil.which("gh") is None:
        raise InstallError(
            "GitHub CLI is required to download the build automatically.\n"
            "Install it from https://github.com/cli/cli#installation, then run "
            "this installer again."
        )

    status = run_command(
        ["gh", "auth", "status", "--hostname", "github.com"],
        capture_output=True,
    )
    if status.returncode == 0:
        return

    print(
        "GitHub CLI needs to sign in once. Follow its instructions; "
        "it will open GitHub in your browser."
    )
    login = run_command(
        ["gh", "auth", "login", "--hostname", "github.com", "--web"]
    )
    if login.returncode != 0:
        raise InstallError(
            "GitHub sign-in did not finish. Run `gh auth login` in a terminal, "
            "then start this installer again."
        )

    status = run_command(
        ["gh", "auth", "status", "--hostname", "github.com"],
        capture_output=True,
    )
    if status.returncode != 0:
        raise InstallError(
            "GitHub CLI sign-in could not be verified. Run `gh auth login` "
            "in a terminal, then start this installer again."
        )


def latest_available_artifact():
    runs_result = run_command(
        [
            "gh",
            "run",
            "list",
            "--repo",
            REPOSITORY,
            "--workflow",
            WORKFLOW,
            "--status",
            "success",
            "--limit",
            "50",
            "--json",
            "databaseId",
        ],
        capture_output=True,
    )
    if runs_result.returncode != 0:
        details = runs_result.stderr.strip() or "GitHub CLI could not list workflow runs."
        raise InstallError(details)

    try:
        runs = json.loads(runs_result.stdout)
    except json.JSONDecodeError as error:
        raise InstallError("GitHub CLI returned an unreadable workflow list.") from error

    for run in runs:
        run_id = str(run["databaseId"])
        artifacts_result = run_command(
            [
                "gh",
                "api",
                f"repos/{REPOSITORY}/actions/runs/{run_id}/artifacts",
                "--jq",
                "[.artifacts[] | select(.expired == false) | "
                f'select(.name | startswith("{ARTIFACT_PREFIX}"))]',
            ],
            capture_output=True,
        )
        if artifacts_result.returncode != 0:
            details = artifacts_result.stderr.strip() or "Could not check build downloads."
            raise InstallError(details)

        try:
            artifacts = json.loads(artifacts_result.stdout)
        except json.JSONDecodeError as error:
            raise InstallError("GitHub CLI returned an unreadable artifact list.") from error

        artifacts = [
            artifact
            for artifact in artifacts
            if artifact["name"].endswith("-linux-x86_64")
        ]
        if artifacts:
            artifacts.sort(key=lambda artifact: artifact["created_at"], reverse=True)
            return run_id, artifacts[0]["name"]

    raise InstallError(
        "No unexpired successful portable build was found. "
        "Ask the project maintainer to run the Portable build artifacts workflow."
    )


def choose_install_parent():
    title = "Choose where to install Swordfish"
    if shutil.which("zenity"):
        result = run_command(
            [
                "zenity",
                "--file-selection",
                "--directory",
                "--title",
                title,
                "--filename",
                f"{Path.home()}/",
            ],
            capture_output=True,
        )
        if result.returncode != 0:
            return None
        return Path(result.stdout.strip()).expanduser()

    if shutil.which("kdialog"):
        result = run_command(
            ["kdialog", "--getexistingdirectory", str(Path.home()), title],
            capture_output=True,
        )
        if result.returncode != 0:
            return None
        return Path(result.stdout.strip()).expanduser()

    try:
        import tkinter as tk
        from tkinter import filedialog
    except ImportError as error:
        tk = None
        tkinter_error = error
    else:
        tkinter_error = None

    if tk is not None:
        try:
            window = tk.Tk()
            window.withdraw()
            window.attributes("-topmost", True)
            selected = filedialog.askdirectory(
                parent=window,
                title=title,
                initialdir=str(Path.home()),
                mustexist=True,
            )
            window.destroy()
            return Path(selected).expanduser() if selected else None
        except tk.TclError as error:
            tkinter_error = error

    if sys.stdin.isatty():
        print("A graphical folder picker is unavailable.")
        selected = input(f"Type the folder to install in [{Path.home()}]: ").strip()
        return Path(selected or Path.home()).expanduser()
    raise InstallError(
        "A graphical folder picker is unavailable. Install `zenity` or "
        "`python3-tk`, or run this installer from a terminal."
    ) from tkinter_error


def extract_portable_archive(archive_path, destination):
    destination_resolved = destination.resolve()
    try:
        with zipfile.ZipFile(archive_path) as archive:
            members = archive.infolist()
            if archive.testzip() is not None:
                raise InstallError("The portable ZIP download is damaged.")

            for member in members:
                member_path = PurePosixPath(member.filename)
                if (
                    member_path.is_absolute()
                    or ".." in member_path.parts
                    or not member_path.parts
                    or member_path.parts[0] != "Swordfish-Portable"
                    or "\\" in member.filename
                ):
                    raise InstallError("The portable ZIP contains an unsafe file path.")

                target = destination.joinpath(*member_path.parts)
                target_resolved = target.resolve()
                if (
                    target_resolved != destination_resolved
                    and destination_resolved not in target_resolved.parents
                ):
                    raise InstallError("The portable ZIP contains an unsafe file path.")

                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue

                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                mode = (member.external_attr >> 16) & 0o777
                if mode:
                    target.chmod(mode)
    except (zipfile.BadZipFile, zlib.error) as error:
        raise InstallError("The portable ZIP download is not a valid ZIP file.") from error

    app_directory = destination / "Swordfish-Portable"
    app_run = app_directory / "AppRun"
    if not app_run.is_file() or not os.access(app_run, os.X_OK):
        raise InstallError("The downloaded bundle is missing its runnable AppRun file.")
    return app_directory


def install_and_start():
    if sys.platform != "linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise InstallError("This portable build currently supports 64-bit x86 Linux only.")

    ensure_github_cli()
    install_parent = choose_install_parent()
    if install_parent is None:
        print("Installation cancelled.")
        return
    if not install_parent.is_dir() or not os.access(install_parent, os.W_OK):
        raise InstallError(f"The selected folder is not writable: {install_parent}")

    print("Finding the latest successful portable build...")
    run_id, artifact_name = latest_available_artifact()
    version = artifact_name[len(ARTIFACT_PREFIX) : -len("-linux-x86_64")]
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", version):
        raise InstallError("The portable build has an invalid version name.")
    install_directory = install_parent / f"Swordfish-Portable-{version}"
    if install_directory.exists():
        raise InstallError(
            f"That version is already installed here:\n{install_directory}\n"
            "Choose a different parent folder, or start the existing copy with AppRun."
        )

    with tempfile.TemporaryDirectory(prefix="swordfish-download-") as temporary:
        temporary_directory = Path(temporary)
        print(f"Downloading Swordfish {version}...")
        download = run_command(
            [
                "gh",
                "run",
                "download",
                run_id,
                "--repo",
                REPOSITORY,
                "--name",
                artifact_name,
                "--dir",
                str(temporary_directory),
            ]
        )
        if download.returncode != 0:
            raise InstallError("The portable build could not be downloaded.")

        archive_path = temporary_directory / (
            f"Swordfish-Portable-{version}-linux-x86_64.zip"
        )
        if not archive_path.is_file():
            raise InstallError("The downloaded build did not contain its portable ZIP file.")

        with tempfile.TemporaryDirectory(
            prefix=".swordfish-install-", dir=install_parent
        ) as staging:
            app_directory = extract_portable_archive(archive_path, Path(staging))
            try:
                app_directory.rename(install_directory)
            except FileExistsError as error:
                raise InstallError(
                    f"An installation already exists here:\n{install_directory}\n"
                    "It was not changed."
                ) from error

    print(f"Installed to: {install_directory}")
    print("Starting Swordfish. Leave this terminal open while the app is running.")
    result = run_command([str(install_directory / "AppRun")])
    if result.returncode != 0:
        raise InstallError(
            f"Swordfish exited with status {result.returncode}. "
            f"The installation is still available at:\n{install_directory}"
        )


def main():
    try:
        install_and_start()
    except InstallError as error:
        print(f"\nSwordfish installer: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"\nSwordfish installer: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nInstallation cancelled.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
