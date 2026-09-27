#pragma once
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// ========================================================================================================
// telescope-measure — el núcleo de la herramienta (prompt 101, F3 de TELESCOPE 0.2; contrato D-100).
//
// EYEPIECE lleva esta herramienta adentro y la llama: nunca linkea el motor. Le manda pedidos por la entrada
// estándar, uno por línea (JSON Lines), y recibe una línea de JSON por pedido, en el mismo orden. La
// especificación que lee EYEPIECE es measure/SCHEMA.md; acá está el código que la cumple, y en
// tests/MeasureTest.cpp una prueba (con su control) por cada regla.
//
// Vive aparte del main() para que el runner de tests lo pruebe en el mismo proceso. Lo que sólo se ve desde
// afuera (el exit, que no escribe en disco, los mismos bytes entre dos corridas) lo prueba
// measure/check-measure-cli.sh contra el binario de verdad.
// ========================================================================================================
namespace telescope
{
class RangeAnalyzer;
}

namespace telescope::measure
{
inline constexpr int kSchema = 1;

// La versión y el sha del motor que midió. Viajan en cada línea (`engine`).
struct EngineInfo
{
    std::string version;   // la de plugins/telescope/VERSION
    std::string sha;       // el commit con que se compiló (12 hex), con "+dirty" si el árbol tenía cambios
};

// La del binario: la escribe EngineVersion.cmake en cada build (ver measure/CMakeLists.txt).
EngineInfo builtEngine();

// Los códigos fijos. Son el vocabulario del contrato: EYEPIECE los traduce (es/en/ja) y un código que no conoce
// lo muestra igual. Están listados, con su significado, en SCHEMA.md; MEASURE[codigos] exige que cada uno esté
// ahí y que SCHEMA.md no nombre ninguno que el código no tenga.
const std::vector<std::string>& refusalCodes();
const std::vector<std::string>& warningCodes();

// Un número con `decimals` decimales fijos, sin depender del locale y sin "-0.0": redondea al más cercano con
// los medios lejos del cero (llround) y arma el texto con enteros.
std::string fixed (double value, int decimals);

// La sesión: un motor (se reusa entre pedidos y se resetea en cada uno) y la versión que firma las líneas.
class Session
{
public:
    explicit Session (EngineInfo engine);
    ~Session();

    // Una línea de pedido → una línea de respuesta, sin el salto de línea.
    std::string handle (const std::string& requestLine);

private:
    EngineInfo engine;
    std::unique_ptr<RangeAnalyzer> analyzer;   // en el heap: el motor es grande y la pila de Windows es de 1 MB
};

// Lee pedidos hasta el fin de la entrada y escribe una línea por pedido (con flush). Devuelve el exit de la
// herramienta: 0 si corrió, aunque haya negado pedidos; otro número sólo si no pudo escribir su salida.
int run (std::istream& in, std::ostream& out, const EngineInfo& engine);
}
