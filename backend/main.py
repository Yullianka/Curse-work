import paho.mqtt.client as mqtt
import sqlite3
import json
import time
from datetime import datetime, timezone, timedelta

BROKER = "broker.hivemq.com"
PORT = 1883
TOPIC = "esp/sensor/data"

def connect_db():
    conn = sqlite3.connect('sensor_data.db')
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS sensor_data (
                    id INTEGER PRIMARY KEY,
                    timestamp TEXT,
                    temperature REAL,
                    brightness REAL,
                    humidity REAL,
                    pressure REAL
                )''')
    conn.commit()
    return conn, c

def on_message(client, userdata, msg):
    try:
        if not msg.payload:
            print("Received empty message")
            return

        data = json.loads(msg.payload.decode())

        temperature = data.get('temperature', None)
        brightness = data.get('lux', None)
        humidity = data.get('humidity', None)
        pressure = data.get('pressure', None)

        time_ = datetime.now(timezone(timedelta(hours=3))).strftime("%Y-%m-%d %H:%M:%S")

        conn, c = connect_db()

        c.execute("SELECT COUNT(*) FROM sensor_data")
        count = c.fetchone()[0]

        max_record = 500
        if count >= max_record:
            c.execute("DELETE FROM sensor_data WHERE id = (SELECT id FROM sensor_data ORDER BY id ASC LIMIT 1)")

        c.execute('''INSERT INTO sensor_data (timestamp, temperature, brightness, humidity, pressure) 
                     VALUES (?, ?, ?, ?, ?)''',
                  (time_, temperature, brightness, humidity, pressure))
        conn.commit()
        print(f"Data inserted: {time_}, {temperature}, {brightness}, {humidity}, {pressure}")
        conn.close()
    except json.JSONDecodeError as e:
        print(f"JSON Decode Error: {e} - Payload: {msg.payload}")
    except Exception as e:
        print(f"Error processing message: {e}")


client = mqtt.Client()
client.on_message = on_message
client.connect(BROKER, PORT)
client.subscribe(TOPIC)
client.loop_start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Exiting...")
    client.loop_stop()
