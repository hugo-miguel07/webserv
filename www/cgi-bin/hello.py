#!/usr/bin/env python3
import os
body = "hello from cgi " + os.environ.get("REQUEST_METHOD", "?") + " q=" + os.environ.get("QUERY_STRING", "")
print("Content-Type: text/plain")
print("")
print(body)
