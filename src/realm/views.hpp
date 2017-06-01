/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef REALM_VIEWS_HPP
#define REALM_VIEWS_HPP

#include <realm/column.hpp>
#include <realm/handover_defs.hpp>

#include <vector>

namespace realm {

const int64_t detached_ref = -1;

class RowIndexes;

// SortDescriptor encapsulates a reference to a set of columns (possibly over links), which is
// used to indicate the criteria columns for sort and distinct. Although the input is column
// indices, it does not rely on those indices remaining stable as long as the columns continue to exist.
class SortDescriptor {
public:
    SortDescriptor() = default;
    SortDescriptor(SortDescriptor const&) = default;
    SortDescriptor(SortDescriptor&&) = default;
    SortDescriptor& operator=(SortDescriptor const&) = default;
    SortDescriptor& operator=(SortDescriptor&&) = default;

    // Create a sort descriptor for the given columns on the given table.
    // Each vector in `column_indices` represents a chain of columns, where
    // all but the last are Link columns (n.b.: LinkList and Backlink are not
    // supported), and the final is any column type that can be sorted on.
    // `column_indices` must be non-empty, and each vector within it must also
    // be non-empty. `ascending` must either be empty or have one entry for each
    // column index chain.
    SortDescriptor(Table const& table, std::vector<std::vector<size_t>> column_indices,
                   std::vector<bool> ascending = {});

    // returns whether this descriptor is valid and can be used to sort
    explicit operator bool() const noexcept
    {
        return !m_columns.empty();
    }
    // returns whether this descriptor has any custom ascending/descending order
    bool has_custom_order() const noexcept
    {
        return std::any_of(m_ascending.begin(), m_ascending.end(), [](bool b) { return !b; });
    }
    void merge_with(SortDescriptor&& other);
    class Sorter;
    Sorter sorter(IntegerColumn const& row_indexes) const;

    // handover support
    std::vector<std::vector<size_t>> export_column_indices() const;
    std::vector<bool> export_order() const;
private:
    std::vector<std::vector<const ColumnBase*>> m_columns;
    std::vector<bool> m_ascending;
};

// Distinct uses the same syntax as sort except that the order is meaningless.
typedef SortDescriptor DistinctDescriptor;

class DescriptorOrdering {
public:
    DescriptorOrdering() = default;
    DescriptorOrdering(DescriptorOrdering const&) = default;
    DescriptorOrdering(DescriptorOrdering&&) = default;
    DescriptorOrdering& operator=(DescriptorOrdering const&) = default;
    DescriptorOrdering& operator=(DescriptorOrdering&&) = default;

    void emplace_sort(SortDescriptor&& sort);
    void emplace_distinct(DistinctDescriptor&& distinct);
    bool descriptor_is_sort(size_t index) const;
    bool descriptor_is_distinct(size_t index) const;
    bool is_empty() const { return m_descriptors.empty(); }
    size_t size() const { return m_descriptors.size(); }
    const SortDescriptor& operator[](size_t ndx) const;
    bool will_apply_sort() const;

    // handover support
    using HandoverPatch = std::unique_ptr<DescriptorOrderingHandoverPatch>;
    static void generate_patch(DescriptorOrdering const&, HandoverPatch&);
    static DescriptorOrdering create_from_and_consume_patch(HandoverPatch&, Table const&);

private:
    std::vector<SortDescriptor> m_descriptors;
};

// This class is for common functionality of ListView and LinkView which inherit from it. Currently it only
// supports sorting and distinct.
class RowIndexes {
public:
    RowIndexes(IntegerColumn::unattached_root_tag urt, realm::Allocator& alloc);
    RowIndexes(IntegerColumn&& col);
    RowIndexes(const RowIndexes& source, ConstSourcePayload mode);
    RowIndexes(RowIndexes& source, MutableSourcePayload mode);

    virtual ~RowIndexes()
    {
#ifdef REALM_COOKIE_CHECK
        m_debug_cookie = 0x7765697633333333; // 0x77656976 = 'view'; 0x33333333 = '3333' = destructed
#endif
    }

    // Disable copying, this is not supported.
    RowIndexes& operator=(const RowIndexes&) = delete;
    RowIndexes(const RowIndexes&) = delete;

    // Return a column of the table that m_row_indexes are pointing at (which is the target table for LinkList and
    // parent table for TableView)
    virtual const ColumnBase& get_column_base(size_t index) const = 0;

    virtual size_t size() const = 0;

    // These two methods are overridden by TableView and LinkView.
    virtual uint_fast64_t sync_if_needed() const = 0;
    virtual bool is_in_sync() const
    {
        return true;
    }

    void check_cookie() const
    {
#ifdef REALM_COOKIE_CHECK
        REALM_ASSERT_RELEASE(m_debug_cookie == cookie_expected);
#endif
    }

    IntegerColumn m_row_indexes;

protected:
    void do_sort(const DescriptorOrdering& ordering);

    static const uint64_t cookie_expected = 0x7765697677777777ull; // 0x77656976 = 'view'; 0x77777777 = '7777' = alive
    uint64_t m_debug_cookie;
};

} // namespace realm

#endif // REALM_VIEWS_HPP
