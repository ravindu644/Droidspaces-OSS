#!/usr/bin/env python3
"""
Generate contributors.json for the Droidspaces Android app.
Run before building the APK:

    GITHUB_TOKEN=ghp_xxx python3 scripts/generate_contributors.py

Requires: pip install requests
"""

import base64
import json
import os
import re
from collections import defaultdict

import requests

REPO   = "ravindu644/Droidspaces-OSS"
OUTPUT = os.path.join(os.path.dirname(__file__),
                      "../Android/app/src/main/assets/contributors.json")
TOKEN  = os.environ.get("GITHUB_TOKEN", "")
HDRS   = {
    "Accept": "application/vnd.github+json",
    **({"Authorization": f"Bearer {TOKEN}"} if TOKEN else {}),
}
BOTS        = {"weblate", "copilot"}
COAUTHOR_RE = re.compile(r"co-authored-by:\s*.+?\s*<([^>]+)>", re.IGNORECASE)
NOREPLY_RE  = re.compile(r"(\d+)\+(.+)@users\.noreply\.github\.com")


def paginate(url):
    params = {"per_page": 100}
    while url:
        r = requests.get(url, headers=HDRS, params=params)
        r.raise_for_status()
        yield from r.json()
        url    = r.links.get("next", {}).get("url")
        params = {}


def resolve_noreply(email):
    """Fast-path: extract login directly from noreply address."""
    m = NOREPLY_RE.match(email)
    if not m:
        return None
    r = requests.get(f"https://api.github.com/users/{m.group(2)}", headers=HDRS)
    if r.ok:
        u = r.json()
        return u["login"], u["avatar_url"]
    return None


def resolve_email(email):
    """Search GitHub by email (works only for publicly linked emails)."""
    resolved = resolve_noreply(email)
    if resolved:
        return resolved
    r = requests.get("https://api.github.com/search/users",
                     headers=HDRS, params={"q": f"{email} in:email"})
    if r.ok:
        items = r.json().get("items", [])
        if items:
            return items[0]["login"], items[0]["avatar_url"]
    return None


def avatar_b64(url):
    sep = "&" if "?" in url else "?"
    r = requests.get(f"{url}{sep}s=48", headers={"Accept": "image/png"})
    r.raise_for_status()
    return base64.b64encode(r.content).decode()


def main():
    print(f"Fetching commits for {REPO}...")
    contribs = defaultdict(lambda: {"commits": 0, "avatar_url": None})

    def add(login, avatar_url):
        if login.lower() in BOTS:
            return
        contribs[login]["commits"] += 1
        if not contribs[login]["avatar_url"]:
            contribs[login]["avatar_url"] = avatar_url

    for commit in paginate(f"https://api.github.com/repos/{REPO}/commits"):
        author     = commit.get("author")
        git_author = commit.get("commit", {}).get("author", {})
        message    = commit.get("commit", {}).get("message", "")

        # Linked GitHub user
        if author and author.get("type") != "Bot":
            add(author["login"], author["avatar_url"])
        # Unlinked --author email fallback
        elif not author and git_author.get("email"):
            resolved = resolve_email(git_author["email"])
            if resolved:
                add(*resolved)

        # Co-authored-by trailers
        for email in COAUTHOR_RE.findall(message):
            resolved = resolve_email(email)
            if resolved:
                add(*resolved)

    print(f"Resolved {len(contribs)} contributors. Fetching avatars...")
    result = []
    for login, data in sorted(contribs.items(),
                               key=lambda x: x[1]["commits"], reverse=True):
        b64 = ""
        if data["avatar_url"]:
            try:
                b64 = avatar_b64(data["avatar_url"])
            except Exception as e:
                print(f"  Warning: avatar failed for {login}: {e}")
        result.append({
            "login":      login,
            "commits":    data["commits"],
            "github_url": f"https://github.com/{login}",
            "avatar_b64": b64,
        })
        print(f"  {login}: {data['commits']} commits")

    os.makedirs(os.path.dirname(os.path.abspath(OUTPUT)), exist_ok=True)
    with open(OUTPUT, "w") as f:
        json.dump(result, f, indent=2)
    print(f"\nWrote {len(result)} contributors → {OUTPUT}")


if __name__ == "__main__":
    main()
