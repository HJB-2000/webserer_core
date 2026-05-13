#!/usr/bin/env python3
"""Crash before sending headers so webserv serves the 500 error page."""
def crash_now():
	# Raise an exception before any headers/body are written.
	raise RuntimeError("Intentional crash before headers")

crash_now()
