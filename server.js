const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const sqlite3 = require('sqlite3').verbose();
const cors = require('cors');
const path = require('path');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static('public')); // For serving web dashboard

// Database setup
const db = new sqlite3.Database('lora_data.db');

// Create tables
db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS sensor_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nodeId INTEGER,
        voltage REAL,
        messageType TEXT,
        timestamp INTEGER,
        rssi INTEGER,
        snr REAL,
        gatewayId TEXT,
        received_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )`);
    
    db.run(`CREATE TABLE IF NOT EXISTS gateways (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        gatewayId TEXT UNIQUE,
        lastSeen DATETIME DEFAULT CURRENT_TIMESTAMP,
        status TEXT DEFAULT 'online'
    )`);
});

// Store connected WebSocket clients
const connectedClients = new Set();

// WebSocket connection handling
wss.on('connection', (ws) => {
    console.log('New WebSocket connection established');
    connectedClients.add(ws);
    
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            console.log('Received WebSocket message:', data);
            
            switch(data.type) {
                case 'gateway_connected':
                    handleGatewayConnection(data);
                    break;
                case 'sensor_data':
                    handleSensorData(data);
                    break;
                default:
                    console.log('Unknown message type:', data.type);
            }
        } catch (error) {
            console.error('Error parsing WebSocket message:', error);
        }
    });
    
    ws.on('close', () => {
        console.log('WebSocket connection closed');
        connectedClients.delete(ws);
    });
    
    ws.on('error', (error) => {
        console.error('WebSocket error:', error);
        connectedClients.delete(ws);
    });
});

// HTTP API Routes

// Receive sensor data via HTTP POST
app.post('/api/sensor-data', (req, res) => {
    console.log('Received sensor data via HTTP:', req.body);
    
    const data = req.body;
    saveSensorData(data);
    
    // Broadcast to all connected WebSocket clients (web dashboard)
    broadcastToClients({
        type: 'new_sensor_data',
        data: data
    });
    
    res.json({ status: 'success', message: 'Data received' });
});

// Get latest sensor data
app.get('/api/sensor-data/latest', (req, res) => {
    const limit = req.query.limit || 50;
    
    db.all(`SELECT * FROM sensor_data ORDER BY received_at DESC LIMIT ?`, [limit], (err, rows) => {
        if (err) {
            res.status(500).json({ error: err.message });
            return;
        }
        res.json(rows);
    });
});

// Get sensor data by node ID
app.get('/api/sensor-data/node/:nodeId', (req, res) => {
    const nodeId = req.params.nodeId;
    const limit = req.query.limit || 50;
    
    db.all(`SELECT * FROM sensor_data WHERE nodeId = ? ORDER BY received_at DESC LIMIT ?`, 
           [nodeId, limit], (err, rows) => {
        if (err) {
            res.status(500).json({ error: err.message });
            return;
        }
        res.json(rows);
    });
});

// Get voltage readings for a specific time range
app.get('/api/sensor-data/voltage/:nodeId', (req, res) => {
    const nodeId = req.params.nodeId;
    const hours = req.query.hours || 24; // Default last 24 hours
    
    const query = `
        SELECT voltage, timestamp, received_at 
        FROM sensor_data 
        WHERE nodeId = ? AND messageType = 'voltage_reading' 
        AND received_at > datetime('now', '-${hours} hours')
        ORDER BY received_at ASC
    `;
    
    db.all(query, [nodeId], (err, rows) => {
        if (err) {
            res.status(500).json({ error: err.message });
            return;
        }
        res.json(rows);
    });
});

// Send command to specific node via LoRa
app.post('/api/send-command', (req, res) => {
    const { nodeId, message } = req.body;
    
    if (!nodeId || !message) {
        res.status(400).json({ error: 'nodeId and message are required' });
        return;
    }
    
    // Send command to gateway via WebSocket
    const command = {
        type: 'send_to_node',
        nodeId: parseInt(nodeId),
        message: message
    };
    
    broadcastToClients(command);
    
    res.json({ status: 'success', message: 'Command sent to gateway' });
});

// Get gateway status
app.get('/api/gateways', (req, res) => {
    db.all(`SELECT * FROM gateways ORDER BY lastSeen DESC`, (err, rows) => {
        if (err) {
            res.status(500).json({ error: err.message });
            return;
        }
        res.json(rows);
    });
});

// Serve web dashboard
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
});

// Helper functions
function handleGatewayConnection(data) {
    console.log('Gateway connected:', data.gateway_id);
    
    // Update gateway status in database
    db.run(`INSERT OR REPLACE INTO gateways (gatewayId, lastSeen, status) 
            VALUES (?, datetime('now'), 'online')`, [data.gateway_id]);
}

function handleSensorData(data) {
    console.log('Processing sensor data:', data);
    saveSensorData(data);
    
    // Broadcast to web clients
    broadcastToClients({
        type: 'new_sensor_data',
        data: data
    });
}

function saveSensorData(data) {
    const stmt = db.prepare(`INSERT INTO sensor_data 
        (nodeId, voltage, messageType, timestamp, rssi, snr, gatewayId) 
        VALUES (?, ?, ?, ?, ?, ?, ?)`);
    
    stmt.run([
        data.nodeId,
        data.voltage,
        data.messageType,
        data.timestamp,
        data.rssi,
        data.snr,
        data.gatewayId
    ]);
    
    stmt.finalize();
}

function broadcastToClients(message) {
    const messageStr = JSON.stringify(message);
    
    connectedClients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(messageStr);
        }
    });
}

// Start server
const PORT = process.env.PORT || 3000;
const WS_PORT = 3001;

server.listen(PORT, () => {
    console.log(`HTTP Server running on port ${PORT}`);
    console.log(`WebSocket Server running on port ${WS_PORT}`);
    console.log(`Dashboard available at http://localhost:${PORT}`);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('Shutting down server...');
    db.close();
    server.close();
    process.exit(0);
});