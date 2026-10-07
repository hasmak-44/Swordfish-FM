#!/usr/bin/env python3

import json
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser


REPOSITORY = "hasmak-44/Swordfish-FM"
WORKFLOW = "portable-release.yml"
ARTIFACT_PREFIX = "Swordfish-Portable-"
API_BASE = f"https://api.github.com/repos/{REPOSITORY}"


class DownloadLookupError(Exception):
    pass


def get_json(url):
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "Swordfish-Portable-Download-Helper",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
        raise DownloadLookupError(f"Could not contact GitHub: {error}") from error


def find_latest_build():
    workflow_path = urllib.parse.quote(WORKFLOW, safe="")
    runs_url = (
        f"{API_BASE}/actions/workflows/{workflow_path}/runs"
        "?status=success&per_page=100"
    )
    runs = get_json(runs_url).get("workflow_runs", [])

    for run in runs:
        artifacts_url = f"{API_BASE}/actions/runs/{run['id']}/artifacts?per_page=100"
        artifacts = get_json(artifacts_url).get("artifacts", [])
        portable_artifacts = [
            artifact
            for artifact in artifacts
            if artifact["name"].startswith(ARTIFACT_PREFIX)
            and artifact["name"].endswith("-linux-x86_64")
            and not artifact["expired"]
        ]
        if portable_artifacts:
            portable_artifacts.sort(
                key=lambda artifact: artifact["created_at"], reverse=True
            )
            return run, portable_artifacts[0]

    raise DownloadLookupError(
        "No unexpired successful portable build was found. "
        "Please try again later or contact the project maintainer."
    )


def main():
    try:
        run, artifact = find_latest_build()
    except DownloadLookupError as error:
        print(f"Swordfish download helper: {error}", file=sys.stderr)
        return 1

    run_url = run["html_url"]
    print(f"Latest portable build: {artifact['name']}")
    print("Opening its GitHub page in your browser.")
    print("On that page, click the matching name under Artifacts to download it.")
    print(f"If the browser does not open, visit:\n{run_url}")

    try:
        opened = webbrowser.open(run_url, new=2)
    except webbrowser.Error as error:
        print(f"Could not open a browser automatically: {error}", file=sys.stderr)
        return 1
    if not opened:
        print("The browser did not open automatically; use the link printed above.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
