import json
import time
import urllib.request
import argparse

def simulate_day_scenario(mock_url="http://localhost:8080"):
    print(f"🎬 Début du scénario de simulation dynamique sur {mock_url}...\n")
    
    scenarios = [
        ("Nuit", "night", 10),
        ("Lever du soleil", "surplus", 15),
        ("Plein midi (Surplus)", "surplus", 20),
        ("Passage en import / Consommation four", "import", 15),
        ("Retour au calme", "surplus", 10),
    ]

    for name, profile, duration_sec in scenarios:
        print(f"⏱  Basculement sur l'événement : '{name}' (profil: {profile}) pour {duration_sec}s")
        req = urllib.request.Request(
            f"{mock_url}/api/profile",
            data=json.dumps({"name": profile}).encode("utf-8"),
            headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(req) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                print(f"   ↳ Mock Envoy mis à jour : {data}")
        except Exception as e:
            print(f"   ⚠️ Impossible de contacter le Mock Server : {e}")
            return

        time.sleep(duration_sec)
    
    print("\n✅ Scénario dynamique terminé avec succès.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Pilotage de scénario sur le Mock Envoy")
    parser.add_argument("--url", default="http://localhost:8080", help="URL du Mock Server")
    args = parser.parse_args()
    simulate_day_scenario(args.url)
