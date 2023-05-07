// import { show_welcome } from "./screens/welcome_screen.js?version=0";

var chatContainer = document.getElementById("chat-container");
var promptInput = document.getElementById("prompt-input");
var sendButton = document.getElementById("send-button");

// var ws; 

function addBotMessage(message) {
    var msg = document.createElement("DIV");
    msg.classList.add("message");
    msg.classList.add("bot");
    
    let converter = new showdown.Converter();
    msg.innerHTML = converter.makeHtml(message);
    chatContainer.appendChild(msg);
    chatContainer.scrollTop = chatContainer.scrollHeight;
}

function addUserMessage(message) {
    var msg = document.createElement("DIV");
    msg.classList.add("message");
    msg.classList.add("user");
    
    let converter = new showdown.Converter();
    msg.innerHTML = converter.makeHtml(message);
    chatContainer.appendChild(msg);
    chatContainer.scrollTop = chatContainer.scrollHeight;
}

function onSend() {
    let prompt = promptInput.value;
    console.log(prompt);
    // addUserMessage(prompt);
    promptInput.value = "";
    ws.send(prompt);
    promptInput.focus();
}

function show_markdown_body(container, task, body) {
    let converter = new showdown.Converter();
    let m = document.createElement("DIV");
    m.innerHTML = converter.makeHtml(body);
    m.classList.add("message");
    container.appendChild(m);
}

function init() {
    sendButton.onclick = onSend;
    promptInput.addEventListener("keypress", function(event) {
        if(event.key === "Enter") {
            event.preventDefault();
            sendButton.click();
        }
    });
    promptInput.focus();
}

init();


var ws = new WebSocket("ws://localhost:3010/chat");
ws.onmessage = function(e) { addBotMessage(e.data); return false;};
ws.onclose = function(e) { console.log("closed"); };
ws.onopen = function(e) { /*e.target.send("Yo!");*/ return false;};




