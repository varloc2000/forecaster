Kind: task
Technical Requirement Document (TRD): ../TRD/02-web-ui-initial-configuration-TRD.md

Important: Create TRD description and put it into a TRD file before implementing the task. Task implementation should be based on TRD. If implementation adjusted during the process - TRD update is required.

Description: When board is started and have no configured WIFI credentials and weather api token - need to provide a way for user to make custom configuration web page available for user to choose wifi SSID and password as well as weather api token.

Requirements:
Add new mode CONFIGURE
Use CONFIGURE mode after device powered if device configurations are not availabe yet.
Use CONNECTING mode after device powered if device configurations are available.

Device behavior in CONFIGURE mode:
Start custom wifi access point with SSID forecaster_wifi and simple hardcoded password Forec@ster
Serve configuration html web page accessible via forecaster.local for devices connected to forecaster_wifi
Configuration web page should contain simple form elements:
Title: Forecaster Light Configuration
Home WIFI: text input
WIFI Password: password input
openweathermap.org API key: password input - prefilled with hardcoded 11f0d20175f478a5b78664b124859ad9
link to https://openweathermap.org/ target blank
Save - button

values provided in form have to be saved as device configuration in device memory so it can be read next time device is powered on.
Once saved device have to be automatically reset so it can detect configurations and proceed with CONNECTING mode.

Add a "Factory Reset" button to Existing web UI in @data/index.html
Click on Factory Reset should show standard confirmation modal
If Confirmed - remove device configurations and allow device to be loaded in CONFIGURE mode
If modal cancelled - close the modal without any additional actions.