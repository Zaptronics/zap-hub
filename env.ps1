$ssid = 'TP-Link_F962'
$password = 'kaleukulele'
$zapee = 'V:\OneDrive\Documents\Electronics\ZTPICO2\Firmware\ZapEE'

# Persist for future processes
[Environment]::SetEnvironmentVariable('ZAP_HUB_WIFI_SSID', $ssid, 'User')
[Environment]::SetEnvironmentVariable('ZAP_HUB_WIFI_PASSWORD', $password, 'User')
[Environment]::SetEnvironmentVariable('ZAPEE_SOURCE', $zapee, 'User')

# Also set for this PowerShell process
$env:ZAP_HUB_WIFI_SSID = $ssid
$env:ZAP_HUB_WIFI_PASSWORD = $password
$env:ZAPEE_SOURCE = $zapee

Write-Host "Zap Hub development environment configured."
Write-Host "WiFi SSID : $env:ZAP_HUB_WIFI_SSID"
Write-Host "ZapEE path : $env:ZAPEE_SOURCE"

