# Remaining work after the ZTPICO2 migration

- User: regenerate dependency integration with the current Zap, then build/test.
- Inspect any compiler/test feedback from the unbuilt Hub 0.3.0 / ZapEE 0.7.0 source.
- Complete a pin-by-pin KiCad connectivity audit if changing physical board wiring;
  this migration carries forward the existing firmware mapping.
- Decide whether commands should be queued into the main application loop; network
  services currently retain synchronous callbacks and a firmware-lifetime instance.
- Implement chamber sensor acquisition/state and coordinated cooling/pumping/HV
  control as separate application work. No new control policy is implied here.
- Add TCA9548/DS28E07 support only when the product requires those board features.
- Publish/re-pin the new ZapEE release when the user is ready; current Hub source
  intentionally consumes ../ZapEE. Hardware regression is skipped.
