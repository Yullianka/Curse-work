from flask import Flask, render_template
import sqlite3

app = Flask(__name__)
DB_PATH = 'sensor_data.db'

def clear_old_data():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("DELETE FROM sensor_data")
    conn.commit()
    conn.close()

clear_old_data()  # This clears previous data when the app starts

def get_all_data():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT * FROM sensor_data ORDER BY timestamp DESC")
    rows = c.fetchall()
    conn.close()
    return rows

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data")
def data():
    sensor_data = get_all_data()
    return render_template("all_data.html", data=sensor_data)

if __name__ == "__main__":
    app.run(debug=True)
