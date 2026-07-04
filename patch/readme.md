
### Modify sdk files and generate patch files.

```bash
# Generate an idempotent, unified diff
diff -u build/_deps/mcp_sdk-src/src/lifecycle/session.cpp patch/mcp_sdk-src/src/lifecycle/session.cpp > patch/mcp_sdk-src/src/lifecycle/session.cpp.patch
```