#include "common.hh"

#include "xcb_atoms.hh"
#include "xcb_connection.hh"
#include "xcb_event.hh"

#include <map>
#include <vector>

namespace xcb {

namespace {

class AtomsImpl : public Atoms {
public:
  explicit AtomsImpl(shared_conn conn)
    : conn_(conn), storage_(std::make_shared<StorageImpl>()) {}

  Reference get(std::string atom) override {
    auto it = index_.find(atom);
    size_t index;
    if (it == index_.end()) {
      index = cookie_.size();
      cookie_.push_back(
          xcb_intern_atom(conn_.get(), 0, atom.size(), atom.c_str()));
      index_.emplace(std::move(atom), index);
    } else {
      index = it->second;
    }
    return Reference(storage_, index);
  }

  bool sync() override {
    std::vector<xcb_atom_t> atoms;
    atoms.reserve(cookie_.size());
    for (auto const& cookie : cookie_) {
      xcb::reply<xcb_intern_atom_reply_t> reply(
          xcb_intern_atom_reply(conn_.get(), cookie, nullptr));
      if (!reply)
        return false;
      atoms.push_back(reply->atom);
    }
    storage_->set(std::move(atoms));
    return true;
  }

private:
  class StorageImpl : public Storage {
  public:
    xcb_atom_t get(size_t id) const override {
      assert(id < resolved_.size());
      return resolved_[id];
    }

    void set(std::vector<xcb_atom_t> resolved) {
      resolved_ = std::move(resolved);
    }

  private:
    std::vector<xcb_atom_t> resolved_;
  };

  shared_conn conn_;
  std::map<std::string, size_t> index_;
  std::vector<xcb_intern_atom_cookie_t> cookie_;
  std::shared_ptr<StorageImpl> storage_;
};

}  // namespace

std::unique_ptr<Atoms> Atoms::create(shared_conn conn) {
  return std::make_unique<AtomsImpl>(conn);
}

}  // namespace xcb
