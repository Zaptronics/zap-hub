# Hub 0.3.1 - migration header cleanup

- Remove seven forwarding headers: ztmb/bus.h, cs_mux.h, cs_lines.h, io_exp.h,
  zap_adapter.h, and drivers/pcal6416.h and tla2518.h.
- Update Hub sources to include the ZapEE public headers directly.
- Remove the unused driver include from the application ADC interface.
- Add ../ZapEE/include to VS Code's fallback include paths for standalone headers.
  CMake already supplies that include directory to compiled translation units.
- Use the user's PATH-installed Zap executable in the README.

The missing-header report points to editor include resolution: the referenced
ZapEE cs_mux.h exists locally. No build or test was run to confirm diagnostics.
No Git operations were performed. ZapEE source and its version are unchanged.
