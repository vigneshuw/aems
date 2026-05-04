"""aemsctl command-line interface package.

The legacy board-interface-cli entry point used ``board_interface.cli:main``.
Keep that import path mapped to the interactive CLI so older editable installs
do not break before the package is reinstalled.
"""

from ..interactive_cli import main

__all__ = ["main"]
