const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const sqlite3 = require('sqlite3').verbose();
const cors = require('cors');
const path = require('path');

const app = express();
const server = http.createServer(app);

// WebSocket server on the same port as HTTP
const wss = new WebSocket.Server({ server });

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static('public')); // For serving web dashboard

console.log('Starting LoRa Gateway Server...');

// Database setup
const db = new sqlite3.Database('lora_data.db');
console.log('Database initialized');

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
    console.log('Database tables ready');
});

// Store connected WebSocket clients
const connectedClients = new Map();

// WebSocket connection handling
wss.on('connection', (ws, req) => {
    const clientId = Date.now() + Math.random();
    connectedClients.set(clientId, {
        socket: ws,
        type: 'unknown',
        connectedAt: new Date(),
        lastPing: new Date()
    });
    
    console.log(`New WebSocket connection: ${clientId} from ${req.socket.remoteAddress}`);
    console.log(`Total connections: ${connectedClients.size}`);
    
    // Send welcome message
    ws.send(JSON.stringify({
        type: 'connection_established',
        clientId: clientId,
        timestamp: Date.now()
    }));
    
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            console.log(`Message from ${clientId}:`, data);
            
            // Update client info based on message type
            const client = connectedClients.get(clientId);
            if (client) {
                client.lastPing = new Date();
                if (data.type === 'gateway_connected') {
                    client.type = 'gateway';
                    client.gatewayId = data.gateway_id;
                }
            }
            
            switch(data.type) {
                case 'gateway_connected':
                    handleGatewayConnection(data, clientId);
                    break;
                case 'sensor_data':
                    handleSensorData(data);
                    break;
                case 'heartbeat':
                    handleHeartbeat(data, clientId);
                    break;
                default:
                    console.log('Unknown message type:', data.type);
            }
        } catch (error) {
            console.error(`Error parsing message from ${clientId}:`, error);
        }
    });
    
    ws.on('close', () => {
        const client = connectedClients.get(clientId);
        console.log(`WebSocket connection closed: ${clientId} (${client?.type || 'unknown'})`);
        connectedClients.delete(clientId);
        console.log(`Remaining connections: ${connectedClients.size}`);
    });
    
    ws.on('error', (error) => {
        console.error(`WebSocket error for ${clientId}:`, error);
        connectedClients.delete(clientId);
    });
    
    // Keep-alive ping
    const pingInterval = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) {
            ws.ping();
        } else {
            clearInterval(pingInterval);
        }
    }, 30000);
});

// HTTP API Routes

// Health check endpoint
app.get('/health', (req, res) => {
    res.json({ 
        status: 'ok', 
        timestamp: Date.now(),
        connections: connectedClients.size,
        uptime: process.uptime()
    });
});

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
    
    res.json({ status: 'success', message: 'Data received', timestamp: Date.now() });
});

// Get latest sensor data
app.get('/api/sensor-data/latest', (req, res) => {
    const limit = req.query.limit || 50;
    
    db.all(`SELECT * FROM sensor_data ORDER BY received_at DESC LIMIT ?`, [limit], (err, rows) => {
        if (err) {
            console.error('Database error:', err);
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
            console.error('Database error:', err);
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
            console.error('Database error:', err);
            res.status(500).json({ error: err.message });
            return;
        }
        res.json(rows);
    });
});

// Send command to specific node via LoRa
app.post('/api/send-command', (req, res) => {
    const { nodeId, message } = req.body;
    
    console.log(`API request to send command: nodeId=${nodeId}, message="${message}"`);
    
    if (!nodeId || !message) {
        res.status(400).json({ error: 'nodeId and message are required' });
        return;
    }
    
    // Send command to gateway via WebSocket
    const command = {
        type: 'send_to_node',
        nodeId: parseInt(nodeId),
        message: message,
        timestamp: Date.now()
    };
    
    const sent = broadcastToGateways(command);
    
    if (sent > 0) {
        console.log(`Command sent to ${sent} gateway(s)`);
        res.json({ 
            status: 'success', 
            message: 'Command sent to gateway', 
            gateways_notified: sent 
        });
    } else {
        console.log('No gateways connected to send command');
        res.status(503).json({ 
            error: 'No gateways connected',
            message: 'Command could not be delivered'
        });
    }
});

// Get gateway status
app.get('/api/gateways', (req, res) => {
    db.all(`SELECT * FROM gateways ORDER BY lastSeen DESC`, (err, rows) => {
        if (err) {
            console.error('Database error:', err);
            res.status(500).json({ error: err.message });
            return;
        }
        
        // Add real-time connection status
        const gatewaysWithStatus = rows.map(gateway => {
            const isConnected = Array.from(connectedClients.values())
                .some(client => client.gatewayId === gateway.gatewayId);
            
            return {
                ...gateway,
                realtime_status: isConnected ? 'connected' : 'disconnected'
            };
        });
        
        res.json(gatewaysWithStatus);
    });
});

// Route to return number of active end nodes
app.get('/api/active-nodes', (req, res) => {
    const fiveMinutesAgo = Date.now() - 5 * 60 * 1000;

    db.all(`
        SELECT DISTINCT nodeId 
        FROM sensor_data 
        WHERE timestamp > ?
    `, [fiveMinutesAgo], (err, rows) => {
        if (err) {
            console.error('DB Error:', err);
            res.status(500).json({ error: 'Internal server error' });
        } else {
            res.json({ activeNodeCount: rows.length });
        }
    });
});


// Get connected clients info
app.get('/api/connections', (req, res) => {
    const connections = Array.from(connectedClients.entries()).map(([id, client]) => ({
        id: id,
        type: client.type,
        gatewayId: client.gatewayId || null,
        connectedAt: client.connectedAt,
        lastPing: client.lastPing
    }));
    
    res.json(connections);
});

// Serve web dashboard
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
});

// Helper functions
function handleGatewayConnection(data, clientId) {
    console.log(`Gateway connected: ${data.gateway_id} (client: ${clientId})`);
    
    // Update gateway status in database
    db.run(`INSERT OR REPLACE INTO gateways (gatewayId, lastSeen, status) 
            VALUES (?, datetime('now'), 'online')`, [data.gateway_id], (err) => {
        if (err) {
            console.error('Database error updating gateway:', err);
        }
    });
    
    // Send acknowledgment
    const client = connectedClients.get(clientId);
    if (client && client.socket.readyState === WebSocket.OPEN) {
        client.socket.send(JSON.stringify({
            type: 'gateway_ack',
            message: 'Gateway connection registered',
            timestamp: Date.now()
        }));
    }
}

function handleHeartbeat(data, clientId) {
    console.log(`Heartbeat from gateway: ${data.gateway_id}`);
    
    // Update last seen time
    db.run(`UPDATE gateways SET lastSeen = datetime('now') WHERE gatewayId = ?`, 
           [data.gateway_id]);
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
    ], function(err) {
        if (err) {
            console.error('Database error saving sensor data:', err);
        } else {
            console.log(`Sensor data saved with ID: ${this.lastID}`);
        }
    });
    
    stmt.finalize();
}

function broadcastToClients(message) {
    const messageStr = JSON.stringify(message);
    let sent = 0;
    
    connectedClients.forEach((client, id) => {
        if (client.socket.readyState === WebSocket.OPEN) {
            client.socket.send(messageStr);
            sent++;
        }
    });
    
    console.log(`Broadcast message sent to ${sent} clients`);
    return sent;
}

function broadcastToGateways(message) {
    const messageStr = JSON.stringify(message);
    let sent = 0;
    
    connectedClients.forEach((client, id) => {
        if (client.type === 'gateway' && client.socket.readyState === WebSocket.OPEN) {
            client.socket.send(messageStr);
            sent++;
            console.log(`Command sent to gateway: ${client.gatewayId}`);
        }
    });
    
    return sent;
}

// Start server
const PORT = process.env.PORT || 3000;

server.listen(PORT, '0.0.0.0', () => {
    console.log(`Server running on port ${PORT}`);
    console.log(`HTTP API available at http://localhost:${PORT}`);
    console.log(`WebSocket server running on the same port`);
    console.log(`Dashboard available at http://localhost:${PORT}`);
    console.log('Waiting for gateway connections...');
});

// Periodic cleanup of stale connections
setInterval(() => {
    const now = new Date();
    let cleaned = 0;
    
    connectedClients.forEach((client, id) => {
        // Remove connections that haven't pinged in 2 minutes
        if (client.socket.readyState !== WebSocket.OPEN || 
            (now - client.lastPing) > 120000) {
            connectedClients.delete(id);
            cleaned++;
        }
    });
    
    if (cleaned > 0) {
        console.log(`Cleaned up ${cleaned} stale connections`);
    }
}, 60000); // Run every minute

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('Shutting down server...');
    
    // Close all WebSocket connections
    connectedClients.forEach((client) => {
        client.socket.close();
    });
    
    db.close((err) => {
        if (err) {
            console.error('Error closing database:', err);
        } else {
            console.log('Database closed');
        }
    });
    
    server.close(() => {
        console.log('Server closed');
        process.exit(0);
    });
});