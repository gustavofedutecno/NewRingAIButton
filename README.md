# _Ring Project Edutecno_

Este repositorio corresponde a los archivos correspondientes al hardware del prototipo del dispositivo _Ring AI_ de Edutecno. El propósito de la botonera es proporcionar una interfaz física para iteractuar con diversos agentes de inteligencia artificial, ya sea a través de una pregunta sencilla o realizar otras tareas de consulta. El equipo consta de un botón que indica cuándo se realiza una consulta, un micrófono para captar el audio de la consulta, un parlante para reproducir el audio resultante desde la aplicación y 2 botones adicionales que afectan el volumen del parlante.

## Elementos del equipo

El equipo prototipo consta de los siguientes componentes:

- ESP32-WROOM-32D: Microcontrolador que se encarga de correr el loop general del sistema, así como los periféricos y la conexión Bluetooth al teléfono.
- Micrófono MEMS INMP441: Micrófono de pequeño tamaño con interfaz de conexión digital I2S hacia el ESP32.
- Módulo Amplificador TDA2030A: Integrado que amplifica señales análogas provenientes del ESP32 hacia un parlante.
- Parlante: Elemento encargado de convertir la señal análoga de audio a sonido que el usuario puede escuchar.
- Botones: Permiten al usuario interactuar con el dispositivo, ya sea para subir/bajar el volumen o indicar al microcontrolador que se va a realizar una consulta.
- LED: Indican al usuario cuando el dispositivo está en modo de consulta.

## Código

El código del dispositivo cuenta de 3 partes, _main.c_, _mic.c_ y _speaker.c_. _mic.c_ maneja la inicialización del micrófono MEMS y establece parámetros tales como la frecuencia de sampleo y profundidad de bytes de la grabación. _speaker.c_ se encarga de inicializar el periférico DAC para la reproducción de audio a través del amplificador externo. Finalmente, en _main.c_ se encuentra el loop principal del dispositivo así como el manejo de _Bluetooth_ para el envío de datos hacia el teléfono a través de la interfaz serial.

## Cómo correr el código

Para ejecutar el código, es necesario descargar el software [Visual Studio Code](https://code.visualstudio.com/download) y haber instalado la extensión llamada "ESP-IDF". Una vez completado este paso se debe compilar y subir a la placa los archivos dentro de la carpeta "main". Los esquemas de conexión de los componentes están descritos en cada uno de los archivos .c correspondientes.
