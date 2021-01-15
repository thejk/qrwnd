#ifndef ARGS_HH
#define ARGS_HH

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Option {
public:
  virtual ~Option() = default;

  virtual bool is_set() const = 0;
  virtual std::string const& arg() const = 0;

protected:
  Option() = default;
  Option(Option const&) = delete;
  Option& operator=(Option const&) = delete;
};

class Args {
public:
  virtual ~Args() = default;

  static std::unique_ptr<Args> create();

  // Returned Option is owned by Args instance.
  virtual Option const* add_option(
      char short_name,
      std::string long_name,
      std::string description) = 0;

  virtual Option const* add_option_with_arg(
      char short_name,
      std::string long_name,
      std::string description,
      std::string arg_description) = 0;

  virtual bool run(int argc, char** argv, std::string_view prgname,
                   std::ostream& err, std::vector<std::string>* out) = 0;

  virtual void print_descriptions(std::ostream& out,
                                  uint32_t column_width) const = 0;

protected:
  Args() = default;
  Args(Args const&) = delete;
  Args& operator=(Args const&) = delete;
};

#endif  // ARGS_HH
