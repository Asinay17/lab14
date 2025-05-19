// Se desactivan las advertencias de seguridad de MSVC para usar funciones antiguas de C
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <iomanip>
#include <curl/curl.h>  // Biblioteca para realizar peticiones HTTP

using namespace std;

// Función para eliminar espacios en blanco alrededor de un string
// Útil para limpiar entradas de usuario y datos del archivo
string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return (start == string::npos || end == string::npos) ? "" : str.substr(start, end - start + 1);
}

// Carga el conocimiento desde un archivo de texto al mapa
// Formato esperado: "pregunta|respuesta" en cada línea
void cargarConocimiento(map<string, string>& conocimiento, const string& nombreArchivo) {
    ifstream archivo(nombreArchivo);
    if (!archivo.is_open()) {
        cout << "Error al abrir el archivo de conocimiento." << endl;
        return;
    }

    string linea;
    while (getline(archivo, linea)) {
        size_t separador = linea.find("|");
        if (separador != string::npos) {
            string pregunta = trim(linea.substr(0, separador));
            string respuesta = trim(linea.substr(separador + 1));
            conocimiento[pregunta] = respuesta;  // Almacena en el diccionario
        }
    }
    archivo.close();
}

// Búsqueda exacta insensible a mayúsculas/minúsculas
// Compara normalizando ambas cadenas a minúsculas
string buscarExacto(const map<string, string>& conocimiento, const string& pregunta) {
    string preguntaLower = pregunta;
    transform(preguntaLower.begin(), preguntaLower.end(), preguntaLower.begin(), ::tolower);

    for (const auto& par : conocimiento) {
        string claveLower = par.first;
        transform(claveLower.begin(), claveLower.end(), claveLower.begin(), ::tolower);

        if (preguntaLower == claveLower) {
            return par.second;
        }
    }
    return "";
}

// Divide una frase en palabras individuales en minúsculas
// Utilizado para el sistema de coincidencia por palabras clave
vector<string> dividirPalabras(const string& frase) {
    vector<string> palabras;
    stringstream ss(frase);
    string palabra;
    while (ss >> palabra) {
        transform(palabra.begin(), palabra.end(), palabra.begin(), ::tolower);
        palabras.push_back(palabra);
    }
    return palabras;
}

// Busca la mejor coincidencia basada en palabras clave comunes
// Evalúa cuántas palabras coinciden entre la pregunta y el conocimiento
string buscarPorPalabrasClave(const map<string, string>& conocimiento, const string& pregunta) {
    vector<string> palabrasPregunta = dividirPalabras(pregunta);
    string mejorRespuesta = "";
    int maxCoincidencias = 0;

    for (const auto& par : conocimiento) {
        vector<string> palabrasBase = dividirPalabras(par.first);

        int coincidencias = 0;
        // Comparación simple de palabras (podría mejorarse con stemming o sinónimos)
        for (const string& palabraPregunta : palabrasPregunta) {
            for (const string& palabraBase : palabrasBase) {
                if (palabraPregunta == palabraBase) {
                    coincidencias++;
                }
            }
        }

        if (coincidencias > maxCoincidencias) {
            maxCoincidencias = coincidencias;
            mejorRespuesta = par.second;
        }
    }
    return (maxCoincidencias > 0) ? mejorRespuesta : "";
}

// Función de búsqueda simple en archivo (grep básico)
// Muestra líneas que contienen la palabra clave con su número de línea
void buscarCoincidencias(const char* archivo, const char* palabraClave) {
    ifstream file(archivo);
    if (!file.is_open()) {
        cerr << "Error abriendo el archivo" << endl;
        return;
    }

    string linea;
    int numeroLinea = 1;

    while (getline(file, linea)) {
        if (linea.find(palabraClave) != string::npos) {
            cout << "Coincidencia en línea " << numeroLinea << ": " << linea << endl;
        }
        numeroLinea++;
    }
    file.close();
}

// Estructura auxiliar para almacenar la respuesta de la API
struct RespuestaAPI {
    char* datos;     // Buffer dinámico para los datos recibidos
    size_t tamano;   // Tamaño actual de los datos
};

// Callback para cURL que escribe la respuesta en nuestra estructura
static size_t escribirRespuesta(void* contenido, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    RespuestaAPI* resp = static_cast<RespuestaAPI*>(userp);

    // Realocamos el buffer para acomodar los nuevos datos
    char* ptr = reinterpret_cast<char*>(realloc(resp->datos, resp->tamano + total + 1));
    if (!ptr) return 0;

    resp->datos = ptr;
    memcpy(&(resp->datos[resp->tamano]), contenido, total);
    resp->tamano += total;
    resp->datos[resp->tamano] = '\0';  // Null-terminator para formar string válido
    return total;
}

// Escapa caracteres especiales para formato JSON
// Necesario para prevenir errores de parsing en la API
string escape_json(const string& s) {
    ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); c++) {
        switch (*c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
            // ... otros casos de escape ...
        default:
            if ('\x00' <= *c && *c <= '\x1f') {
                o << "\\u" << hex << setw(4) << setfill('0') << (int)*c;
            }
            else {
                o << *c;
            }
        }
    }
    return o.str();
}

// Realiza una consulta a la API de OpenAI usando cURL
// Implementa: autenticación, envío de JSON y procesamiento de respuesta
string consultarOpenAI(const string& pregunta) {
    CURL* curl;
    CURLcode res;
    RespuestaAPI respuesta = { nullptr, 0 };

    // IMPORTANTE: Reemplazar con API key válida!
    const string api_key = "Bearer Aquí va el API-KEY que usted dió Ingeniero, copie y pegue su API-KEY aquí";
    if (api_key == "Bearer error") {
        return "ERROR: Por favor configura tu API key de OpenAI en el código (reemplaza en const string)";
    }

    const string url = "https://api.openai.com/v1/chat/completions";
    string json = "{"
        "\"model\": \"gpt-3.5-turbo\","
        "\"messages\": [{\"role\": \"user\", \"content\": \"" + escape_json(pregunta) + "\"}],"
        "\"temperature\": 0.7"
        "}";

    // Configuración de cURL
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: " + api_key).c_str());

        // Configura opciones de cURL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // ... otras configuraciones ...

        // Ejecuta la petición HTTP POST
        res = curl_easy_perform(curl);

        string resultado;
        if (res != CURLE_OK) {
            resultado = "Error al conectar con OpenAI: " + string(curl_easy_strerror(res));
        }
        else if (respuesta.datos) {
            // Procesamiento de la respuesta JSON
            string respuesta_json = respuesta.datos;

            // Extracción del contenido de la respuesta
            size_t contenido_pos = respuesta_json.find("\"content\":\"");
            if (contenido_pos != string::npos) {
                // ... lógica de procesamiento ...
            }
        }

        // Limpieza de recursos de cURL
        curl_easy_cleanup(curl);
        // ... más limpieza ...

        return resultado;
    }
    return "Error al inicializar cURL";
}

// Función principal con el flujo de interacción del usuario
int main() {
    setlocale(LC_ALL, "es_ES.UTF-8");  // Configura localización para español

    map<string, string> conocimiento;
    cargarConocimiento(conocimiento, "conocimiento.txt");  // Carga base de conocimiento

    // Mensaje inicial con instrucciones
    cout << "=== Chatbot Inteligente ===" << endl;
    cout << "Opciones especiales:" << endl;
    cout << " - 'buscar [palabra]': Busca en archivos" << endl;
    cout << " - 'openai [pregunta]': Consulta a OpenAI (extra)" << endl;
    cout << " - 'adios': Terminar el programa" << endl << endl;

    string input;
    while (true) {
        cout << "\nTu: ";
        getline(cin, input);

        // Comandos especiales
        if (input == "adios") {
            cout << "Bot: Hasta luego!" << endl;
            break;
        }

        if (input.rfind("buscar ", 0) == 0) {
            // Búsqueda en archivo local
            string palabra = input.substr(7);
            cout << "=== Resultados de búsqueda ===" << endl;
            buscarCoincidencias("conocimiento.txt", palabra.c_str());
            continue;
        }

        if (input.rfind("openai ", 0) == 0) {
            // Consulta a OpenAI
            string pregunta = input.substr(7);
            cout << "Consultando a OpenAI...\n";
            string respuesta = consultarOpenAI(pregunta);
            cout << "Bot (OpenAI): " << respuesta << endl;
            continue;
        }

        // Lógica principal de respuesta
        string respuesta = buscarExacto(conocimiento, input);
        if (respuesta.empty()) {
            respuesta = buscarPorPalabrasClave(conocimiento, input);
        }

        // Respuesta por defecto si no encuentra coincidencias
        if (respuesta.empty()) {
            respuesta = "No sé la respuesta. ¿Quieres intentar con 'openai [tu pregunta]'?";
        }

        cout << "Bot: " << respuesta << endl;
    }

    return 0;
}