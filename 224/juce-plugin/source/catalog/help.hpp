// Slider tooltips from the owner's manuals: page/param_help.json and the
// lookup of `helpFor` in page/app.js, ported line for line (the channel/output
// suffix of page/check_param_help.py, the page heading that says what a
// "LEVEL n" or "DELAY n" means, and the signal-path sentence).
#pragma once
#include "json.hpp"

#include <map>
#include <string>
#include <string_view>

namespace lexcat {

class ParamHelp {
public:
    ParamHelp() = default;
    // Throws JsonError on malformed input.
    static ParamHelp parse(std::string_view paramHelpJson);

    // helpFor(name, heading): the tooltip for a slider named `name` on a page
    // headed `heading`; empty if the manuals have nothing for it.
    std::string helpFor(std::string_view name, std::string_view heading) const;

    bool empty() const { return params_.empty(); }

    // The suffix split of check_param_help.py / app.js: the name with runs of
    // spaces collapsed and trimmed, then without its channel/output suffix.
    // `suffix` receives the suffix itself ("L)AD", "LR", "(L)", ...) or "".
    static std::string stripSuffix(std::string_view name, std::string *suffix = nullptr);

private:
    std::map<std::string, std::string> aliases_;   // display name -> manual name
    std::map<std::string, std::string> params_;    // manual name -> help text
};

}  // namespace lexcat
