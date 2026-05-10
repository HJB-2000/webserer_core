#!/usr/bin/env python3
"""Crash before sending headers so nginx serves /errors/500.html."""
def crash_now():
	# Raise an exception before any headers/body are written.
	raise RuntimeError("Intentional crash before headers")

crash_now()
