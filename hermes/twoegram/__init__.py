"""Twoegram for Hermes Agent: the a2 platform, for an Apple //e.

register() is the plugin's entry point. The protocol modules (protocol,
text, store, server) need only the standard library; adapter.py and cli.py
need Hermes, so they are imported only when Hermes loads the plugin.
"""

VERSION = "1.0.0"


def register(ctx):
    from .adapter import register as _register
    _register(ctx)
