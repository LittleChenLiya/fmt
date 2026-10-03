// Demo and verification for fmt::preview_range.
//
// Compile (single program, no build system needed):
//   g++ -std=c++17 -I include demo_preview.cc -o demo_preview

#include <fmt/ranges.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// A single-pass counting input range: yields 0, 1, 2, ... up to size items.
// It does NOT expose size(), so its total is unknown.
class counting_range {
 public:
  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = const int*;
    using reference = int;

    iterator() : cur_(0), end_(0), reads_(nullptr) {}
    iterator(size_t c, size_t e, size_t* reads)
        : cur_(c), end_(e), reads_(reads) {}

    auto operator*() const -> int { return static_cast<int>(cur_); }
    auto operator++() -> iterator& {
      ++cur_;
      if (reads_) ++(*reads_);
      return *this;
    }
    auto operator++(int) -> iterator {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }
    auto operator==(const iterator& other) const -> bool {
      return cur_ == other.cur_;
    }
    auto operator!=(const iterator& other) const -> bool {
      return cur_ != other.cur_;
    }

   private:
    size_t cur_;
    size_t end_;
    size_t* reads_;
  };

  counting_range(size_t size, size_t* reads) : size_(size), reads_(reads) {}

  auto begin() -> iterator { return iterator(0, size_, reads_); }
  auto end() -> iterator { return iterator(size_, size_, reads_); }

 private:
  size_t size_;
  size_t* reads_;
};

// A one-shot iterator that throws when formatting a specific value.
class throwing_range {
 public:
  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = const int*;
    using reference = int;

    iterator() : cur_(0), throw_at_(0), consumed_(nullptr) {}
    iterator(size_t c, size_t t, size_t* cons)
        : cur_(c), throw_at_(t), consumed_(cons) {}

    auto operator*() const -> int {
      if (cur_ == throw_at_) throw std::runtime_error("boom");
      return static_cast<int>(cur_);
    }
    auto operator++() -> iterator& {
      ++cur_;
      if (consumed_) ++(*consumed_);
      return *this;
    }
    auto operator++(int) -> iterator {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }
    auto operator==(const iterator& other) const -> bool {
      return cur_ == other.cur_;
    }
    auto operator!=(const iterator& other) const -> bool {
      return cur_ != other.cur_;
    }

   private:
    size_t cur_;
    size_t throw_at_;
    size_t* consumed_;
  };

  throwing_range(size_t size, size_t throw_at, size_t* consumed)
      : size_(size), throw_at_(throw_at), consumed_(consumed) {}

  auto begin() -> iterator { return iterator(0, throw_at_, consumed_); }
  auto end() -> iterator { return iterator(size_, throw_at_, consumed_); }

 private:
  size_t size_;
  size_t throw_at_;
  size_t* consumed_;
};

static int failures = 0;

void check(const std::string& name, const std::string& got,
           const std::string& expected) {
  if (got == expected) {
    std::cout << "PASS " << name << ": " << got << "\n";
  } else {
    std::cout << "FAIL " << name << "\n"
              << "  expected: " << expected << "\n"
              << "  got:      " << got << "\n";
    ++failures;
  }
}

int main() {
  {
    auto v = std::vector<int>{0, 1, 2, 3, 4};
    auto s = fmt::format("{}", fmt::preview_range(v, 2));
    check("sized_truncated", s, "[0, 1, ...(5)]");
  }
  {
    auto v = std::vector<int>{0, 1};
    auto s = fmt::format("{}", fmt::preview_range(v, 2));
    check("sized_exact", s, "[0, 1]");
  }
  {
    auto v = std::vector<int>{0, 1, 2};
    auto s = fmt::format("{}", fmt::preview_range(v, 10));
    check("sized_under_limit", s, "[0, 1, 2]");
  }
  {
    size_t reads = 0;
    auto r = counting_range(5, &reads);
    auto s = fmt::format("{}", fmt::preview_range(r, 2));
    check("input_truncated", s, "[0, 1, ...(unknown)]");
    std::cout << "  reads=" << reads << " (expected 3: 2 formatted + 1 probe)\n";
    if (reads != 3) {
      std::cout << "FAIL input_reads: expected 3, got " << reads << "\n";
      ++failures;
    }
  }
  {
    auto v = std::vector<int>{};
    auto s = fmt::format("{}", fmt::preview_range(v, 2));
    check("empty_limit2", s, "[]");
  }
  {
    auto v = std::vector<int>{0, 1, 2};
    auto s = fmt::format("{}", fmt::preview_range(v, 0));
    check("nonempty_limit0", s, "[...(3)]");
  }
  {
    auto v = std::vector<int>{};
    auto s = fmt::format("{}", fmt::preview_range(v, 0));
    check("empty_limit0", s, "[]");
  }
  {
    auto v = std::vector<std::string>{"a\nb", "c\td", "e"};
    auto s = fmt::format("{}", fmt::preview_range(v, 2));
    check("string_escape", s, "[\"a\\nb\", \"c\\td\", ...(3)]");
  }
  {
    auto v = std::vector<int>{1, 2, 3, 4};
    auto s = fmt::format("{:02}", fmt::preview_range(v, 2));
    check("element_specs", s, "[01, 02, ...(4)]");
  }
  {
    // One-shot (single-pass input) iterator: preview_range traverses the range
    // in a single pass and never collects elements into a container.
    size_t reads = 0;
    auto r = counting_range(4, &reads);
    auto s1 = fmt::format("{}", fmt::preview_range(r, 3));
    check("one_shot_first", s1, "[0, 1, 2, ...(unknown)]");
    // A second traversal is a fresh single pass (the range regenerates).
    auto s2 = fmt::format("{}", fmt::preview_range(r, 3));
    check("one_shot_second", s2, "[0, 1, 2, ...(unknown)]");
  }
  {
    size_t consumed = 0;
    auto r = throwing_range(5, 2, &consumed);
    bool threw = false;
    try {
      (void)fmt::format("{}", fmt::preview_range(r, 5));
    } catch (const std::exception& e) {
      threw = true;
      std::cout << "  exception: " << e.what() << "\n";
    }
    if (!threw) {
      std::cout << "FAIL exception: expected throw\n";
      ++failures;
    }
    std::cout << "  consumed=" << consumed
              << " (expected 2: values 0 and 1 consumed, value 2 throws)\n";
    if (consumed != 2) {
      std::cout << "FAIL exception_consumed: expected 2, got " << consumed
                << "\n";
      ++failures;
    }
  }
  {
    auto v = std::vector<int>{0, 1, 2, 3};
    auto s = fmt::format("{}", fmt::preview_range(v.begin(), v.end(), 2));
    check("iter_pair_sized", s, "[0, 1, ...(4)]");
  }
  {
    size_t reads = 0;
    auto r = counting_range(4, &reads);
    auto s = fmt::format("{}", fmt::preview_range(r.begin(), r.end(), 2));
    check("iter_pair_input", s, "[0, 1, ...(unknown)]");
  }
  if (failures == 0) {
    std::cout << "\nALL TESTS PASSED\n";
    return 0;
  }
  std::cout << "\n" << failures << " TEST(S) FAILED\n";
  return 1;
}
