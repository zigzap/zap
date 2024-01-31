var eL = document.getElementById("log");
var eLt = document.getElementById("logtoggler");

var showLog = false;

function toggleLog() {
    if(showLog) {
        eL.style.display = "none";
        eLt.textContent = "(show)";
    } else {
        eL.style.display = "block";
        eLt.textContent = "(hide)";
    }
    showLog = !showLog;
}
function log(s) {
    eL.value += s.toString();
    eL.value += "\n"
    eL.scrollTop = eL.scrollHeight;
}

function sendJSON(data, slug, method="POST") {
    json = JSON.stringify(data);
    log("SENDING: " + json);
    const response = fetch(slug, {
        method: method,
        body: json,
        headers: {
            "Content-Type": "application/json; charset=UTF-8"
        }
    });
    return response;
}

function onNewUser() {
    var first_name = document.getElementById("first_name").value;
    var last_name = document.getElementById("last_name").value;
    data = {
        first_name: first_name,
        last_name: last_name,
    }
    sendJSON(data, "/users", "POST")
    .then((response) => response.json())
    .then((data) => {
        log("SUCCESS: " + JSON.stringify(data));
        getUserList();
    })
    .catch((error) => {
        log("Error posting data");
    });
}

function deleteUser(id) {
    fetch("/users/" + id, { method: "DELETE", } )
    .then((response) => response.json())
    .then((data) => {
        log("SUCCESS: " + JSON.stringify(data));
        getUserList();
    })
    .catch((error) => {
        log("Error deleting data");
    });
}

function changeUser(id) {
    const firstname = document.getElementById("first_" + id).value;
    const lastname = document.getElementById("last_" + id).value;
    data = {
        first_name: firstname,
        last_name: lastname,
    }
    sendJSON(data, "/users/" + id, "PATCH")
    .then((response) => response.json())
    .then((data) => {
        log("SUCCESS: " + JSON.stringify(data));
        getUserList();
    })
    .catch((error) => {
        log("Error updating data");
    });
}

function addHeaderCell(tr, text) {
    var th = document.createElement('TH');
    th.innerHTML = text;
    tr.appendChild(th);
}

function showTable(users) {
    var t = document.getElementById("usertable");
    var new_t = document.createElement("TABLE");
    var header = new_t.createTHead();
    var tr = header.insertRow();
    // insertCell creates TD, we want TH
    addHeaderCell(tr, 'id');
    addHeaderCell(tr, 'first name');
    addHeaderCell(tr, 'last name');
    addHeaderCell(tr, '');
    addHeaderCell(tr, '');

    console.log("showTable()");
    console.log(users);
    // add the data rows
    for(var i=0; i<users.length; i++) {
        var row = new_t.insertRow();
        var c1 = row.insertCell();
        c1.innerHTML = users[i].id;
        var c2 = row.insertCell();
        c2.innerHTML = '<input id="first_' + users[i].id + '" value="' + users[i].first_name + '"></input>';
        var c3 = row.insertCell();
        c3.innerHTML = '<input id="last_' + users[i].id + '" value="' + users[i].last_name + '"></input>';
        var c4 = row.insertCell();
        c4.innerHTML = ''
            + '<form class="tdform"><button  class="updatebutton" type="button" onclick="changeUser(' + users[i].id + ');">change</button></form>'
        ;
        var c5 = row.insertCell();
        c5.innerHTML = ''
            + '<form class="tdform"><button class="delbutton" type="button" onclick="deleteUser(' + users[i].id + ');">del</button></form>'
        ;
    }
    console.log("before replace");
    t.innerHTML = new_t.innerHTML;
    console.log("after replace");
}


function getUserList() {
    fetch("/users", { method: "GET", } )
    .then((response) => response.json())
    .then((data) => {
        log("SUCCESS: " + JSON.stringify(data));
        showTable(data);
    })
    .catch((error) => {
        log("Error fetching data");
    });
}

function init() {
    getUserList();
}
init();

