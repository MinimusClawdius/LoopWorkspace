/*
 * Enhanced Loop CGM Monitor - Pebble JavaScript
 * 
 * Improved version with better error handling and data formatting
 * Communicates with Trio's local HTTP server (127.0.0.1:8080)
 */

var API_BASE = 'http://127.0.0.1:8080';
var REFRESH_INTERVAL = 5 * 60 * 1000; // 5 minutes
var REQUEST_TIMEOUT = 10000; // 10 seconds

// Fetch data from Trio's local server
function fetchCGMData() {
    console.log('Fetching CGM data from ' + API_BASE);
    
    var xhr = new XMLHttpRequest();
    xhr.open('GET', API_BASE + '/api/all', true);
    xhr.timeout = REQUEST_TIMEOUT;
    
    xhr.onload = function() {
        if (xhr.status === 200) {
            try {
                var data = JSON.parse(xhr.responseText);
                console.log('Received data:', data);
                sendDataToWatch(data);
            } catch (e) {
                console.log('JSON parse error: ' + e);
                sendErrorToWatch('Invalid data format');
            }
        } else if (xhr.status === 404) {
            console.log('API endpoint not found - server may not be running');
            sendErrorToWatch('Server not available');
        } else {
            console.log('HTTP error: ' + xhr.status);
            sendErrorToWatch('Server error: ' + xhr.status);
        }
    };
    
    xhr.ontimeout = function() {
        console.log('Request timeout');
        sendErrorToWatch('Request timeout');
    };
    
    xhr.onerror = function() {
        console.log('Request error');
        sendErrorToWatch('Connection error');
    };
    
    xhr.send();
}

// Send formatted data to Pebble watch
function sendDataToWatch(data) {
    var message = {};
    
    // Glucose data
    if (data.glucose && data.glucose.sgv !== null) {
        message.KEY_GLUCOSE = Math.round(data.glucose.sgv);
    } else if (data.glucose && data.glucose.glucose !== null) {
        message.KEY_GLUCOSE = Math.round(data.glucose.glucose);
    }
    
    // Trend data
    if (data.glucose && data.glucose.direction) {
        message.KEY_TREND = data.glucose.direction.rawValue || data.glucose.direction;
    }
    
    // IOB data (from pump manager)
    if (data.insulin && data.insulin.iob !== null) {
        message.KEY_IOB = Math.round(data.insulin.iob * 10); // Store as tenths
    }
    
    // Loop status
    if (data.loop !== undefined) {
        message.KEY_IS_CLOSED_LOOP = data.loop.isClosedLoop ? 1 : 0;
    }
    
    // COB data
    if (data.insulin && data.insulin.cob !== null) {
        message.KEY_COB = Math.round(data.insulin.cob);
    }
    
    // Pump battery
    if (data.pump && data.pump.battery !== null) {
        message.KEY_BATTERY = Math.round(data.pump.battery);
    }
    
    // Timestamp
    if (data.glucose && data.glucose.date) {
        message.KEY_DATA_TIMESTAMP = Math.round(data.glucose.date.getTime() / 1000);
    }
    
    console.log('Sending to watch:', message);
    Pebble.sendAppMessage(message, 
        function() { console.log('Data sent successfully'); },
        function(e) { console.log('Error sending to watch: ' + JSON.stringify(e)); }
    );
}

// Send error state to watch
function sendErrorToWatch(errorMessage) {
    console.log('Sending error to watch:', errorMessage);
    Pebble.sendAppMessage({
        'KEY_GLUCOSE': 0, // Indicates error state
        'KEY_TREND': 'error',
        'KEY_COMMAND_MSG': errorMessage
    });
}

// Handle bolus requests from watch
function handleBolusRequest(unitsRaw) {
    // Convert from raw value (stored as units * 20 for 0.05U precision)
    var units = unitsRaw / 20.0;
    
    // Validate bounds
    if (units < 0.05) units = 0.05;
    if (units > 10.0) units = 10.0;
    
    console.log('Bolus request:', units, 'U');
    
    var xhr = new XMLHttpRequest();
    xhr.open('POST', API_BASE + '/api/bolus', true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.timeout = REQUEST_TIMEOUT;
    
    xhr.onload = function() {
        if (xhr.status === 202) { // Accepted - requires confirmation
            try {
                var response = JSON.parse(xhr.responseText);
                Pebble.sendAppMessage({
                    'KEY_COMMAND_STATUS': 1, // pending confirmation
                    'KEY_COMMAND_MSG': response.message || 'Confirm on iPhone'
                });
            } catch (e) {
                console.log('Parse error:', e);
                Pebble.sendAppMessage({
                    'KEY_COMMAND_STATUS': -1,
                    'KEY_COMMAND_MSG': 'Invalid response'
                });
            }
        } else {
            console.log('Bolus request failed:', xhr.status);
            Pebble.sendAppMessage({
                'KEY_COMMAND_STATUS': -1,
                'KEY_COMMAND_MSG': 'Request failed'
            });
        }
    };
    
    xhr.onerror = function() {
        console.log('Bolus request connection error');
        Pebble.sendAppMessage({
            'KEY_COMMAND_STATUS': -1,
            'KEY_COMMAND_MSG': 'Connection error'
        });
    };
    
    xhr.send(JSON.stringify({ units: units }));
}

// Handle carb requests from watch
function handleCarbRequest(gramsRaw, absorptionHoursRaw) {
    var grams = gramsRaw;
    var absorptionHours = absorptionHoursRaw || 3; // Default 3 hours
    
    // Validate bounds
    if (grams < 5) grams = 5;
    if (grams > 200) grams = 200;
    
    console.log('Carb request:', grams, 'g over', absorptionHours, 'hours');
    
    var xhr = new XMLHttpRequest();
    xhr.open('POST', API_BASE + '/api/carbs', true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.timeout = REQUEST_TIMEOUT;
    
    xhr.onload = function() {
        if (xhr.status === 202) { // Accepted - requires confirmation
            try {
                var response = JSON.parse(xhr.responseText);
                Pebble.sendAppMessage({
                    'KEY_COMMAND_STATUS': 1, // pending confirmation
                    'KEY_COMMAND_MSG': response.message || 'Confirm on iPhone'
                });
            } catch (e) {
                console.log('Parse error:', e);
                Pebble.sendAppMessage({
                    'KEY_COMMAND_STATUS': -1,
                    'KEY_COMMAND_MSG': 'Invalid response'
                });
            }
        } else {
            console.log('Carb request failed:', xhr.status);
            Pebble.sendAppMessage({
                'KEY_COMMAND_STATUS': -1,
                'KEY_COMMAND_MSG': 'Request failed'
            });
        }
    };
    
    xhr.onerror = function() {
        console.log('Carb request connection error');
        Pebble.sendAppMessage({
            'KEY_COMMAND_STATUS': -1,
            'KEY_COMMAND_MSG': 'Connection error'
        });
    };
    
    xhr.send(JSON.stringify({ 
        grams: grams, 
        absorptionHours: absorptionHours 
    }));
}

// Handle incoming messages from Pebble watch
function handleIncomingMessage(e) {
    var payload = e.payload;
    
    if (payload.KEY_REQUEST_DATA) {
        fetchCGMData();
    } else if (payload.KEY_BOLUS_REQUEST !== undefined) {
        handleBolusRequest(payload.KEY_BOLUS_REQUEST);
    } else if (payload.KEY_CARB_REQUEST !== undefined) {
        var absorption = payload.KEY_ABSORPTION_HOURS || 3;
        handleCarbRequest(payload.KEY_CARB_REQUEST, absorption);
    }
}

// Event listeners
Pebble.addEventListener('appmessage', function(e) {
    handleIncomingMessage(e);
});

Pebble.addEventListener('ready', function() {
    console.log('PebbleKit JS ready');
    fetchCGMData();
});

// Auto-refresh data every 5 minutes
setInterval(function() {
    console.log('Auto-refresh interval triggered');
    fetchCGMData();
}, REFRESH_INTERVAL);

// Also fetch when watch wakes up (if supported)
if (Pebble.addEventListener) {
    Pebble.addEventListener('showConfiguration', function() {
        // This would open config page if we had one
        fetchCGMData();
    });
}

console.log('Enhanced Pebble JavaScript loaded');