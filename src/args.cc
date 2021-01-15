#include "common.hh"

#include "args.hh"

#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

class OptionImpl : public Option {
public:
  OptionImpl(char short_name, std::string long_name, std::string description,
             bool require_arg, std::string arg_description)
    : short_name_(short_name),
      long_name_(std::move(long_name)),
      description_(std::move(description)),
      require_arg_(require_arg),
      arg_description_(std::move(arg_description)) {
  }

  bool is_set() const override { return set_; }

  std::string const& arg() const override { return arg_; }

  char short_name() const { return short_name_; }

  std::string const& long_name() const { return long_name_; }

  std::string const& description() const { return description_; }

  bool require_arg() const { return require_arg_; }

  std::string const& arg_description() const { return arg_description_; }

  void reset() {
    set_ = false;
    arg_.clear();
  }

  void set() {
    set_ = true;
  }

  void set_arg(std::string arg) {
    arg_ = std::move(arg);
  }

private:
  char const short_name_;
  std::string const long_name_;
  std::string const description_;
  bool const require_arg_;
  std::string const arg_description_;
  bool set_ = false;
  std::string arg_;
};

class ArgsImpl : public Args {
public:
  ArgsImpl() = default;

  Option const* add_option(char short_name, std::string long_name,
                           std::string description) override {
    prepare_option(short_name, long_name);
    options_.push_back(std::make_unique<OptionImpl>(short_name,
                                                    std::move(long_name),
                                                    std::move(description),
                                                    false, std::string()));
    return options_.back().get();
  }

  Option const* add_option_with_arg(char short_name, std::string long_name,
                                    std::string description,
                                    std::string arg_description) override {
    prepare_option(short_name, long_name);
    options_.push_back(std::make_unique<OptionImpl>(short_name,
                                                    std::move(long_name),
                                                    std::move(description),
                                                    true, arg_description));
    return options_.back().get();
  }

  bool run(int argc, char** argv, std::string_view prgname, std::ostream& err,
           std::vector<std::string>* out) override {
    for (int a = 1; a < argc; ++a) {
      if (argv[a][0] == '-') {
        if (argv[a][1] == '-') {
          if (argv[a][2] != '\0') {
            // A long name with optional "=" argument
            size_t len = 2;
            while (argv[a][len] != '=' && argv[a][len])
              ++len;
            std::string name(argv[a] + 2, len - 2);
            auto it = long_names_.find(name);
            if (it == long_names_.end()) {
              err << prgname << ": unrecognized option '--"
                  << name << "'" << std::endl;
              return false;
            }
            auto* opt = options_[it->second].get();
            opt->set();
            if (argv[a][len]) {
              if (opt->require_arg()) {
                opt->set_arg(std::string(argv[a] + len + 1));
              } else {
                err << prgname << ": option '--"
                    << name << "' doesn't allow an argument" << std::endl;
                return false;
              }
            } else {
              if (opt->require_arg()) {
                if (a + 1 >= argc) {
                  err << prgname << ": option '--"
                      << name << "' requires an argument" << std::endl;
                  return false;
                } else {
                  opt->set_arg(argv[++a]);
                }
              }
            }
            continue;
          } else {
            // "--", all following values are arguments
            for (++a; a < argc; ++a)
              out->push_back(argv[a]);
            break;
          }
        } else if (argv[a][1] != '\0') {
          // One or more short names
          for (auto* name = argv[a] + 1; *name; ++name) {
            auto it = short_names_.find(*name);
            if (it == short_names_.end()) {
              err << prgname << ": invalid option -- '"
                  << *name << "'" << std::endl;
              return false;
            }
            auto* opt = options_[it->second].get();
            opt->set();
            if (opt->require_arg()) {
              if (a + 1 >= argc) {
                err << prgname << ": option requires an argument"
                    << " -- '" << *name << "'" << std::endl;
                return false;
              } else {
                opt->set_arg(argv[++a]);
              }
            }
          }
          continue;
        } else {
          // single "-", treat as argument
        }
      }

      out->push_back(argv[a]);
    }
    return true;
  }

  void print_descriptions(std::ostream& out,
                          uint32_t column_width) const override {
    uint32_t max_left = 0;
    for (auto const& option : options_) {
      uint32_t left = 0;
      if (option->short_name() != '\0') {
        if (!option->long_name().empty()) {
          left = 6 + option->long_name().size();  // -S, --long
        } else {
          left = 2;                               // -S
        }
      } else if (!option->long_name().empty()) {
        left = 2 + option->long_name().size();  // --long
      }
      if (option->require_arg())
        left += 1 + option->arg_description().size();  // (=| )ARG
      if (left > 0)
        left += 2;  // Need at least two spaces between option and desc

      if (left > max_left)
        max_left = left;
    }

    uint32_t const avail_right =
      max_left > column_width ? 0 : column_width - max_left;

    if (avail_right < 20) {
      // Fallback mode, description on its own row.
      for (auto const& option : options_) {
        print_option(out, *option);
        out << '\n' << option->description() << '\n';
      }
      return;
    }

    // Check if all descriptions fit, justify to the right on a 80 col width
    bool all_desc_fit = true;
    uint32_t max_right = 0;
    for (auto const& option : options_) {
      uint32_t right = option->description().size();
      if (right > avail_right) {
        all_desc_fit = false;
        break;
      }
      if (right > max_right)
        max_right = right;
    }

    if (all_desc_fit)
      max_left = std::max(80u, column_width) - max_right;

    for (auto const& option : options_) {
      uint32_t left = print_option(out, *option);
      std::fill_n(std::ostreambuf_iterator<char>(out), max_left - left, ' ');

      if (option->description().size() <= avail_right) {
        out << option->description() << '\n';
        continue;
      }

      // Wrap description
      size_t last = 0;
      bool first = true;
      while (true) {
        if (first) {
          first = false;
        } else {
          std::fill_n(std::ostreambuf_iterator<char>(out), max_left, ' ');
        }

        size_t end = last + avail_right;
        if (end >= option->description().size()) {
          out << option->description() << '\n';
          break;
        }
        size_t space = option->description().rfind(' ', end);
        if (space == std::string::npos || space < last) {
          space = end;
        }
        out << option->description().substr(last, space - last) << '\n';
        last = space < end ? space + 1 : end;
      }
    }
  }

private:
  void prepare_option(char short_name, std::string const& long_name) {
    if (short_name != '\0')
      short_names_.emplace(short_name, options_.size());
    if (!long_name.empty()) {
      assert(long_name.find('=') == std::string::npos);
      long_names_.emplace(long_name, options_.size());
    }
  }

  size_t print_option(std::ostream& out, const OptionImpl& option) const {
    bool only_short = false;
    size_t ret = 0;
    if (option.short_name() != '\0') {
      out << '-' << option.short_name();
      if (!option.long_name().empty()) {
        out << ", --" << option.long_name();
        ret = 6 + option.long_name().size();
      } else {
        ret = 2;
        only_short = true;
      }
    } else if (!option.long_name().empty()) {
      out << "--" << option.long_name();
      ret = 2 + option.long_name().size();
    }
    if (option.require_arg()) {
      out << (only_short ? ' ' : '=') << option.arg_description();
      ret += 1 + option.arg_description().size();
    }
    return ret;
  }

  std::vector<std::unique_ptr<OptionImpl>> options_;
  std::unordered_map<char, size_t> short_names_;
  std::unordered_map<std::string, size_t> long_names_;
};

}  // namespace

std::unique_ptr<Args> Args::create() {
  return std::make_unique<ArgsImpl>();
}
