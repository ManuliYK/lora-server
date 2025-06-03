# LoRA Server Dashboard

A Node.js server with WebSocket functionality for managing and viewing LoRA (Low-Rank Adaptation) inputs with a real-time dashboard interface.

## Features

- Real-time WebSocket communication
- Web-based dashboard interface
- LoRA data management and visualization
- File upload and processing capabilities

## Project Structure

```
├── server.js              # Main server file
├── package.json           # Dependencies and scripts
├── package-lock.json      # Dependency lock file
├── public/               # Static files
│   └── dashboard.html    # Dashboard interface
├── lora_data.db         # Database file
└── node_modules/        # Dependencies (not in repo)
```

## Installation

1. Clone the repository:
```bash
git clone https://github.com/yourusername/lora-server.git
cd lora-server
```

2. Install dependencies:
```bash
npm install
```

3. Start the server:
```bash
npm start
```

4. Open your browser and navigate to:
- Dashboard: http://localhost:3000
- WebSocket Server: ws://localhost:3001

## Environment Variables

- `PORT`: Server port (default: 3000)
- `WS_PORT`: WebSocket port (default: 3001)

## Deployment

This application can be deployed to various cloud platforms:

- **Railway**: Connect GitHub repo and deploy automatically
- **Render**: Free tier available with auto-deployment
- **DigitalOcean**: App Platform or Droplet deployment
- **AWS/Google Cloud**: EC2/Compute Engine instances

## Development

To run in development mode:
```bash
npm run dev
```

## API Endpoints

- `GET /` - Dashboard interface
- WebSocket connection on configured port for real-time updates

## License

MIT License

## Contributing

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request
