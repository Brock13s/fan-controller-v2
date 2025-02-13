// HtmlContent.h
#ifndef HtmlContent_h
#define HtmlContent_h

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>ESP32 Serial Monitor</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #121212;
        color: #e0e0e0;
        margin: 0;
        padding: 0;
      }
      .container {
        width: 600px;
        margin: 50px auto;
        padding: 20px;
        background-color: #1e1e1e;
        box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
        text-align: center;
        border-radius: 4px;
        border: 5px solid transparent;
      }
      h2 { margin-top: 0; color: #ffffff;}
      textarea {
        width: 100%;
        height: 300px;
        resize: none;
        display: block;
        margin: 20px auto;
        background-color: black;
        color: limegreen;
      }
      input[type="text"] {
        width: 40%;
        padding: 20px;
        margin-right: 10px;
        background-color: #2e2e2e;
        border: 1px solid #444;
        border-radius: 10px;
        color: #e0e0e0;
      }
      button {
        padding: 20px 16px;
        background-color: #444;
        border: none;
        border-radius: 10px;
        color: #e0e0e0;
        margin-left: 5px;
        cursor: pointer;
      }
      button:hover{
        background-color: #555;
      }
      .button-group {
        margin-top: 10px;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h2>ESP32 Serial Monitor</h2>
      <textarea id="log" readonly></textarea>
      <div class="button-group">
        <input type="text" id="cmd" placeholder="Enter command">
        <button onclick="sendCommand()" title="Send command to server">Send</button>
        <button onclick="clearLog()" title="Clear web serial console">Clear</button>
        <button onclick="logout()" title="Logout from web serial console">Logout</button>
      </div>
    </div>
    
    <script>
      // Open a WebSocket connection to the ESP32.
      
      var socket;
      var logElement = document.getElementById('log');
      var cmdInput = document.getElementById('cmd');
      var pingInterval = 20000;
      var pingTimer = null;
      var pongTimeoutTimer = null;
      var pongTimeout = 5000;
      
      // When a message is received, append it to the textarea and scroll to bottom.
      function initWebSocket(){
      var wsProtocol = (window.location.protocol === "https:") ? "wss://" : "ws://";
      socket = new WebSocket(wsProtocol + window.location.host + '/ws');

      socket.onmessage = function(event) {
        
        if(event.data === "pong"){
            console.log("Pong reply recieved.");
            if(pongTimeoutTimer){
                clearTimeout(pongTimeoutTimer);
                pongTimeoutTimer = null;
            }
            var container = document.querySelector(".container");
            if(container){
              container.style.border = '5px solid transparent';
            }
        } else {
        logElement.value += event.data;
        logElement.scrollTop = logElement.scrollHeight;
        console.log("Recieved " + event.data);
        }
      };

      socket.onopen = function(event){
        console.log("WebSocket connection established");
        startPingCycle();
      };

      // Log any WebSocket errors to the console.
      socket.onerror = function(error) {
        console.error("WebSocket error:", error);
      };

      // Log the WebSocket close event.
      socket.onclose = function(event) {
        console.log("WebSocket closed:", event);
        stopPingCycle();
      };
    }

    function startPingCycle(){
        if(pingTimer !== null){
            clearInterval(pingTimer);
        }
        pingTimer = setInterval(function(){
            if(socket && socket.readyState === WebSocket.OPEN){
                console.log("Sending ping to server...");
                socket.send("ping");
                pongTimeoutTimer = setTimeout(function(){
                    var container = document.querySelector(".container");
                    if(container){
                      container.style.border = "5px solid red";
                    }
                    setTimeout(function(){
                    if(confirm("Server is down (pong was not received)! Would you like to reload?")){
                      window.location.reload();
                    } else {
                      console.log("User choose not to reconnect.");
                    }
                    }, 50);
                    
                    
                }, pongTimeout);
            }
        }, pingInterval);
    }




      // Send a command via the WebSocket.
      function sendCommand() {
        var cmd = cmdInput.value;
        if (cmd.trim().length > 0) {
          socket.send(cmd);
          document.getElementById('cmd').value = "";
        }
      }

      function clearLog(){
        socket.send("clearlog");
        document.getElementById('log').value = "";
      }
      
      // Logout function: close the WebSocket and redirect to /logout.
      function logout() {
        if (socket && socket.readyState === WebSocket.OPEN) {
          socket.close();
        }
        window.location.href = "/logout";
        console.log("User is logging out. Websocket connection closed");
      }

      cmdInput.addEventListener("keydown", function(event) {
        if (event.key === "Enter") {
          event.preventDefault();  // Prevent default behavior, e.g., form submission.
          sendCommand();
        }
      });
      
      // Optionally, close the WebSocket if the user leaves the page.
      window.addEventListener('beforeunload', function() {
        if(socket && socket.readyState === WebSocket.OPEN){
          socket.close();
        }
      });

      function stopPingCycle(){
        if(pingTimer !== null){
            clearInterval(pingTimer);
            pingTimer = null;
        }
        if(pongTimeoutTimer !== null){
            clearTimeout(pongTimeoutTimer);
            pongTimeoutTimer = null;
        }
    }
      window.addEventListener("load", initWebSocket);
    </script>
  </body>
</html>
)rawliteral";

#endif