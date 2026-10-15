Kind: task
Technical Requirement Document (TRD): ../TRD/03-cleanup-ui-improvements-TRD.md

Important: Create TRD description and put it into a TRD file before implementing the task. Task implementation should be based on TRD. If implementation adjusted during the process - TRD update is required.

Description: Remove unnecessary UI buttons and adjust functions. Fix display issues with color effects

Requirements:
1. Remove CONNECTING button from UI - I can't imagine how it can be useful
2. Remove TESTING button from UI but not remove MODE_TESTING functionality
3. Add small button with only one letter i on top right corner - click on button should show modal with information about the project. 
Button should have absolute position and not shift UI element 
button shoud look like a small circle with i inside 
Use following text as base for modal html:
```html
Forecaster v1.0
Author: Tim Cherniavski
varloc2000@gmail.com

[Reconfigure](on click should show dialog: "Device will switch to setup mode. Reconnect your phone/PC to the 'forecaster_wifi' network to reconfigure." before issuing the POST request once confirmed - reset wifi credentials and put device into MODE_CONFIGURE)

[Demo all weather light modes](on click should put device into MODE_TESTING and close modal)

[Close](close modal on click)
```

4. Cleanup buildStateJson from redundant CONFIGURE button - it never shows up in UI
5. Fetch also wind direction from api and include it into weatherData. Add wind direction into UI as an arrow and letters ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW']
Use simplest html css trick to display an arrow - Use Unicode North Arrow ↑ (U+2191): The standard arrow symbol ↑ natively points straight up to 0° (North) and use css transform: rotate(${windDeg}deg); display: inline-block;

