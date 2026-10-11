"""Checks the GitHub Pages site: every link on the HTML pages resolves to a file in the repository."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGES = ["index.html", "wiring_reference.html", "drive_visualizer.html"]
REPO_BLOB = "https://github.com/mmcgeary/HDR2D2/blob/simplified-droid/"


def links(page):
    return re.findall(r'(?:href|src)="([^"#]+)"', (ROOT / page).read_text())


class SiteLinksTest(unittest.TestCase):
    def test_local_links_exist(self):
        for page in PAGES:
            for link in links(page):
                if not link.startswith(("http://", "https://", "mailto:")):
                    self.assertTrue((ROOT / link).exists(), f"{page}: {link}")

    def test_repository_links_exist(self):
        for page in PAGES:
            for link in links(page):
                if link.startswith(REPO_BLOB):
                    self.assertTrue((ROOT / link[len(REPO_BLOB):]).exists(), f"{page}: {link}")

    def test_home_links_every_tool_and_guide(self):
        home = (ROOT / "index.html").read_text()
        for name in ["wiring_reference.html", "drive_visualizer.html"]:
            self.assertIn(f'href="{name}"', home)
        for guide in sorted(p.name for p in ROOT.glob("*.md") if p.name != "README.md") + ["README.md"]:
            self.assertIn(REPO_BLOB + guide, home, guide)

    def test_tools_link_home(self):
        for page in ["wiring_reference.html", "drive_visualizer.html"]:
            self.assertIn('href="index.html"', (ROOT / page).read_text(), page)


if __name__ == "__main__":
    unittest.main()
