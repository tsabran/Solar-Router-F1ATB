#!/usr/bin/env python3
"""
Mock Server Enphase (Enlighten Cloud + Envoy-S Metered)
Simule les endpoints de l'écosystème Enphase pour le Solar Router F1ATB :
1. Enlighten Cloud : POST /login/login.json  -> Renvoie {"session_id": "..."}
2. Entrez Cloud    : POST /tokens           -> Renvoie le JWT TokenEnphase
3. Envoy-S Local   : GET  /ivp/meters/readings -> Renvoie le JSON de mesures
"""

import sys
import json
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# Profils de test par défaut
PROFILES = {
    "surplus": {
        "solar": 2500.0,
        "grid": -1800.0,
        "voltage": 232.5,
        "freq": 50.0
    },
    "import": {
        "solar": 350.0,
        "grid": 1650.0,
        "voltage": 229.6,
        "freq": 50.02
    },
    "night": {
        "solar": 0.0,
        "grid": 450.0,
        "voltage": 230.0,
        "freq": 50.0
    }
}

CURRENT_PROFILE = PROFILES["surplus"]


def make_meters_readings_payload(solar_w, grid_w, voltage_v=230.0, freq_hz=50.0):
    return [
        {
            "timestamp": 1718000000,
            "eid": 704643072,
            "voltage": voltage_v,
            "activePower": float(solar_w),
            "apparentPower": float(solar_w) + 10.0,
            "reactivePower": 150.0,
            "current": round(solar_w / voltage_v, 2) if voltage_v else 0.0,
            "channels": [
                {
                    "eid": 1778384896,
                    "timestamp": 1718000000,
                    "activePower": float(solar_w),
                    "apparentPower": float(solar_w) + 10.0,
                    "voltage": voltage_v,
                    "current": round(solar_w / voltage_v, 2) if voltage_v else 0.0
                },
                {"voltage": 0.0, "current": 0.0},
                {"voltage": 0.0, "current": 0.0}
            ]
        },
        {
            "timestamp": 1718000000,
            "eid": 704643328,
            "voltage": voltage_v,
            "activePower": float(grid_w),
            "apparentPower": abs(float(grid_w)) + 20.0,
            "reactivePower": -100.0,
            "current": round(abs(grid_w) / voltage_v, 2) if voltage_v else 0.0,
            "freq": freq_hz,
            "actEnergyDlvd": 450000.0,
            "actEnergyRcvd": 320000.0,
            "channels": [
                {
                    "eid": 1778385152,
                    "timestamp": 1718000000,
                    "activePower": float(grid_w),
                    "apparentPower": abs(float(grid_w)) + 20.0,
                    "voltage": voltage_v,
                    "freq": freq_hz,
                    "actEnergyDlvd": 450000.0,
                    "actEnergyRcvd": 320000.0
                },
                {"voltage": 0.0, "current": 0.0},
                {"voltage": 0.0, "current": 0.0}
            ]
        }
    ]


class EnphaseMockHandler(BaseHTTPRequestHandler):
    def _send_json(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        url = urlparse(self.path)

        # 1. Enlighten Login: /login/login.json
        if url.path == "/login/login.json":
            content_len = int(self.headers.get("Content-Length", 0))
            self.rfile.read(content_len)
            print("[Mock Enlighten] Authentification acceptée -> session_id généré")
            self._send_json({
                "session_id": "mock_enlighten_session_id_9999",
                "message": "success"
            })
            return

        # 2. Entrez Token: /tokens
        if url.path == "/tokens":
            content_len = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_len)
            req = json.loads(body.decode("utf-8")) if body else {}
            username = req.get("username", "user")
            print(f"[Mock Entrez] Token demandé pour {username} -> TokenEnphase généré")
            # Faux JWT valide > 50 chars
            fake_jwt = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyIjoidGVzdCJ9.signature_mock_token_over_50_chars"
            self._send_json(fake_jwt)
            return

        # Switch de profil dynamique via API
        if url.path == "/api/profile":
            content_len = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_len)
            req = json.loads(body.decode("utf-8")) if body else {}
            name = req.get("name", "surplus")
            if name in PROFILES:
                global CURRENT_PROFILE
                CURRENT_PROFILE = PROFILES[name]
                print(f"[Mock] Profil basculé sur: {name}")
                self._send_json({"status": "ok", "profile": name})
            else:
                self._send_json({"error": "unknown profile"}, status=400)
            return

        self.send_response(404)
        self.end_headers()

    def do_GET(self):
        url = urlparse(self.path)

        # 3. Envoy-S Readings: /ivp/meters/readings
        if url.path == "/ivp/meters/readings":
            auth = self.headers.get("Authorization", "")
            cookie = self.headers.get("Cookie", "")
            if not auth.startswith("Bearer ") and "sessionId=" not in cookie:
                print("⚠️ [Mock Envoy] Requête refusée: Pas de Bearer Token ni de SessionId Cookie valide")
                self.send_response(401)
                self.end_headers()
                return

            payload = make_meters_readings_payload(
                solar_w=CURRENT_PROFILE["solar"],
                grid_w=CURRENT_PROFILE["grid"],
                voltage_v=CURRENT_PROFILE["voltage"],
                freq_hz=CURRENT_PROFILE["freq"]
            )
            print(f"📊 [Mock Envoy] 200 OK -> Solaire: {CURRENT_PROFILE['solar']}W | Réseau: {CURRENT_PROFILE['grid']}W")
            self._send_json(payload)
            return

        self.send_response(404)
        self.end_headers()


def run_server(port=8080):
    server = HTTPServer(("0.0.0.0", port), EnphaseMockHandler)
    print(f"🚀 Mock Server Enphase actif sur http://0.0.0.0:{port}")
    print("   Endpoints simulés :")
    print("   • POST /login/login.json")
    print("   • POST /tokens")
    print("   • GET  /ivp/meters/readings")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nArrêt du serveur.")
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Mock Server Enphase")
    parser.add_argument("--port", type=int, default=8080, help="Port HTTP d'écoute (défaut: 8080)")
    args = parser.parse_args()
    run_server(args.port)
