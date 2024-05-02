from bluepy.btle import Peripheral, UUID

def main():
    device_address = 'f4:12:fa:9f:f5:72'  # Adresse de votre ESP32
    service_uuid = UUID("0000a000-0000-1000-8000-00805f9b34fb")
    char_temp_uuid = UUID("0000a005-0000-1000-8000-00805f9b34fb")  # UUID de la caractéristique de température
    char_hum_uuid = UUID("0000a003-0000-1000-8000-00805f9b34fb")   # UUID de la caractéristique d'humidité
    char_lux_uuid = UUID("0000a004-0000-1000-8000-00805f9b34fb")   # UUID de la caractéristique de luminosité
    char_write_uuid = UUID("0000a006-0000-1000-8000-00805f9b34fb") # UUID de la caractéristique d'écriture

    p = Peripheral(device_address, 'public')

    try:
        service = p.getServiceByUUID(service_uuid)
        char_temp = service.getCharacteristics(forUUID=char_temp_uuid)[0]
        char_hum = service.getCharacteristics(forUUID=char_hum_uuid)[0]
        char_lux = service.getCharacteristics(forUUID=char_lux_uuid)[0]
        char_write = service.getCharacteristics(forUUID=char_write_uuid)[0]

        # Lire les valeurs des caractéristiques de lecture
        if char_temp.supportsRead():
            temp_value = char_temp.read()
            print(f"Temperature: {temp_value.decode('utf-8')}°C")

        if char_hum.supportsRead():
            hum_value = char_hum.read()
            print(f"Humidity: {hum_value.decode('utf-8')}%")

        if char_lux.supportsRead():
            lux_value = char_lux.read()
            print(f"Luminosity: {lux_value.decode('utf-8')} lux")

        # Interagir pour écrire des données
        while True:
            data_to_write = input("Enter message to send to ESP32 (type 'exit' to quit): ")
            if data_to_write.lower() == 'exit':
                print("Exiting...")
                break

            # Convertir la chaîne en bytes et écrire
            char_write.write(data_to_write.encode('utf-8'))
            print("Data written to the device.")

    finally:
        p.disconnect()

if __name__ == '__main__':
    main()
