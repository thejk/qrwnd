#ifndef XCB_ATOMS_HH
#define XCB_ATOMS_HH

#include "xcb_connection.hh"

#include <memory>
#include <string>
#include <xcb/xproto.h>

namespace xcb {

class Atoms {
protected:
  class Storage;

public:
  virtual ~Atoms() = default;

  class Reference {
  public:
    xcb_atom_t get() {
      return atoms_->get(id_);
    }

    Reference(std::shared_ptr<Storage> atoms, size_t id)
      : atoms_(atoms), id_(id) {}

  private:
    std::shared_ptr<Storage> atoms_;
    size_t id_;
  };

  virtual Reference get(std::string atom) = 0;

  virtual bool sync() = 0;

  static std::unique_ptr<Atoms> create(shared_conn conn);

protected:
  Atoms() = default;
  Atoms(Atoms const&) = delete;
  Atoms& operator=(Atoms const&) = delete;

  class Storage {
  public:
    virtual ~Storage() = default;

    virtual xcb_atom_t get(size_t id) const = 0;

  protected:
    Storage() = default;
  };
};

}  // namespace xcb

#endif  // XCB_ATOMS_HH
