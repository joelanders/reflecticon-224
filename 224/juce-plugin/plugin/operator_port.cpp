// The operator factory (see operator_port.hpp): the operator for the set's
// remote (operator_ports.hpp): the 224XL's LARC operator, the 224X's panel
// operator, the 224's panel operator with the catalog's RAM layout (v4.x, or
// "v3.2"). An unrecognized set (no catalog) gets NoOperator: the direct
// parameters work, operator tasks are refused.
#include "operator_port.hpp"
#include "operator_ports.hpp"

namespace lexplug {

std::unique_ptr<OperatorPort> makeOperatorPort(Engine &engine, const lexcat::Catalog *catalog) {
    if (catalog != nullptr && catalog->remote == lexcat::Remote::Larc) {
        return std::make_unique<LarcPort>(engine);
    }
    if (catalog != nullptr && catalog->remote == lexcat::Remote::Panel) {
        return std::make_unique<PanelPort>(engine);
    }
    if (catalog != nullptr && catalog->remote == lexcat::Remote::Panel224) {
        return std::make_unique<Panel224Port>(engine, catalog->layout.c_str());
    }
    return std::make_unique<NoOperator>(engine);
}

}  // namespace lexplug
