#!/usr/bin/env python3
"""
Generate contributors.json for the Droidspaces Android app.
Run before building the APK:

    GITHUB_TOKEN=ghp_xxx python3 scripts/generate_contributors.py
"""

import base64
import functools
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict

REPO   = "ravindu644/Droidspaces-OSS"
OUTPUT = os.path.join(os.path.dirname(__file__),
                      "../Android/app/src/main/assets/contributors.json")
TOKEN  = os.environ.get("GITHUB_TOKEN", "")
HDRS   = {"Authorization": f"Bearer {TOKEN}"} if TOKEN else {}
BOTS        = {"weblate", "copilot"}
COAUTHOR_RE = re.compile(r"co-authored-by:\s*.+?\s*<([^>]+)>", re.IGNORECASE)
NOREPLY_RE  = re.compile(r"(\d+)\+(.+)@users\.noreply\.github\.com")


def get(url, params=None, accept="application/vnd.github+json"):
    """Open url; raises urllib.error.HTTPError on 4xx/5xx. The token goes to the
    API host only, never to the avatar CDN or wherever it redirects."""
    if params:
        url += ("&" if "?" in url else "?") + urllib.parse.urlencode(params)
    auth = HDRS if url.startswith("https://api.github.com/") else {}
    return urllib.request.urlopen(
        urllib.request.Request(url, headers={**auth, "Accept": accept}))


def get_json(url, params=None):
    for attempt in range(2):
        try:
            with get(url, params) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            # Search allows 30 requests a minute. Wait out the window once
            # rather than dropping every contributor resolved after it.
            reset = e.headers.get("X-RateLimit-Reset")
            if attempt == 0 and e.headers.get("X-RateLimit-Remaining") == "0" and reset:
                time.sleep(min(max(int(reset) - time.time(), 0) + 1, 65))
                continue
            print(f"  Warning: {url}: {e}")
            return None


def paginate(url):
    params = {"per_page": 100}
    while url:
        with get(url, params) as r:
            yield from json.load(r)
            m = re.search(r'<([^>]+)>;\s*rel="next"', r.headers.get("Link", ""))
        url, params = (m.group(1) if m else None), None


def resolve_noreply(email):
    """Fast-path: extract login directly from noreply address."""
    m = NOREPLY_RE.match(email)
    if not m:
        return None
    u = get_json(f"https://api.github.com/users/{m.group(2)}")
    return (u["login"], u["avatar_url"]) if u else None


@functools.cache
def resolve_email(email):
    """Search GitHub by email (works only for publicly linked emails)."""
    resolved = resolve_noreply(email)
    if resolved:
        return resolved
    found = get_json("https://api.github.com/search/users",
                     {"q": f"{email} in:email"})
    items = found.get("items", []) if found else []
    return (items[0]["login"], items[0]["avatar_url"]) if items else None


def avatar_b64(url):
    sep = "&" if "?" in url else "?"
    with get(f"{url}{sep}s=48", accept="image/png") as r:
        return base64.b64encode(r.read()).decode()


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
            resolved = resolve_email(git_author["email"].lower())
            if resolved:
                add(*resolved)

        # Co-authored-by trailers
        for email in COAUTHOR_RE.findall(message):
            resolved = resolve_email(email.lower())
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
