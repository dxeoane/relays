import gi
gi.require_version('Gtk', '3.0')
gi.require_version('AppIndicator3', '0.1')
from gi.repository import Gtk, AppIndicator3
import signal
import paho.mqtt.publish as publish
import logging

logging.basicConfig(level=logging.INFO)

def send_mqtt_message(_):
    # Configuración MQTT
    broker = "mqtt.vagalume.es" 
    topic = "Teletype"
    auth = {'username': 'Teletype', 'password': 'cyCPyxF5nmUd8brt'}
    mensaje = "pulse 0,2000" 

    try:
        publish.single(topic, mensaje, hostname=broker, auth=auth, port=61883)
    except Exception as e:
        logging.error(f"Error enviando mensaje MQTT: {e}")

def quit_app(_):
    Gtk.main_quit()

def main():
    indicator = AppIndicator3.Indicator.new(
        "puerta-indicador",
        "dialog-password", 
        AppIndicator3.IndicatorCategory.APPLICATION_STATUS
    )
    indicator.set_status(AppIndicator3.IndicatorStatus.ACTIVE)

    menu = Gtk.Menu()

    item_mqtt = Gtk.MenuItem(label="Abrir la puerta")
    item_mqtt.connect("activate", send_mqtt_message)
    menu.append(item_mqtt)
    item_mqtt.show()

    item_quit = Gtk.MenuItem(label="Salir")
    item_quit.connect("activate", quit_app)
    menu.append(item_quit)
    item_quit.show()

    menu.show()
    indicator.set_menu(menu)

    # Permitir Ctrl+C en terminal
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    Gtk.main()

if __name__ == "__main__":
    main()
