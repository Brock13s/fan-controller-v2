// HtmlContent.h
#ifndef HtmlContent_h
#define HtmlContent_h

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
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
        padding: 8px;
        margin-right: 10px;
        background-color: #2e2e2e;
        border: 1px solid #444;
        border-radius: 4px;
        color: #e0e0e0;
      }
      button {
        padding: 8px 16px;
        background-color: #444;
        border: none;
        border-radius: 4px;
        color: #e0e0e0;
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
        <button onclick="sendCommand()">Send</button>
        <button onclick="clearLog()">Clear</button>
        <button onclick="logout()">Logout</button>
      </div>
    </div>
    
    <script>
      // Open a WebSocket connection to the ESP32.
      var socket = new WebSocket('ws://' + window.location.host + '/ws');
      var logElement = document.getElementById('log');
      var cmdInput = document.getElementById('cmd');
      
      // When a message is received, append it to the textarea and scroll to bottom.
      socket.onmessage = function(event) {
        logElement.value += event.data;
        logElement.scrollTop = logElement.scrollHeight;
      };

      // Log any WebSocket errors to the console.
      socket.onerror = function(error) {
        console.log("WebSocket error:", error);
      };

      // Log the WebSocket close event.
      socket.onclose = function(event) {
        console.log("WebSocket closed:", event);
      };

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
        if (socket.readyState === WebSocket.OPEN) {
          socket.close();
        }
        window.location.href = "/logout";
      }

      cmdInput.addEventListener("keydown", function(event) {
        if (event.key === "Enter") {
          event.preventDefault();  // Prevent default behavior, e.g., form submission.
          sendCommand();
        }
      });
      
      // Optionally, close the WebSocket if the user leaves the page.
      window.addEventListener('beforeunload', function() {
        socket.close();
      });
    </script>
  </body>
</html>
)rawliteral";

#endif