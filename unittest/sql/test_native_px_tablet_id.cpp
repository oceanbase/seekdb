#include <gtest/gtest.h>
#include <limits>

#include "lib/allocator/page_arena.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_calc_partition_id.h"
#include "sql/engine/px/ob_slice_calc.h"

namespace oceanbase
{
namespace sql
{
namespace
{
class InspectRepartCalc final : public ObRepartSliceIdxCalc
{
public:
  InspectRepartCalc(ObExecContext &context,
      const share::schema::ObTableSchema &schema, const ObPxPartChInfo &channels)
      : ObRepartSliceIdxCalc(context, schema, nullptr, nullptr, nullptr, nullptr,
          ObPQDistributeMethod::LOCAL, ObNullDistributeMethod::NONE, channels,
          OB_REPARTITION_NO_REPARTITION)
  {}
  void set_cached_tablet(int64_t value) { tablet_id_ = value; }
};
}

TEST(NativePxTabletId, unsupported_base_keeps_output)
{
  common::ObArenaAllocator owner;
  ObSliceIdxCalc calculator(owner, ObNullDistributeMethod::NONE);
  int64_t output = 85;
  EXPECT_EQ(common::OB_NOT_SUPPORTED, calculator.get_previous_row_tablet_id(output));
  EXPECT_EQ(85, output);
}

TEST(NativePxTabletId, cached_signed_value_and_sentinels_are_copied)
{
  common::ObArenaAllocator owner;
  ObExecContext context(owner);
  share::schema::ObTableSchema schema;
  ObPxPartChInfo channels;
  InspectRepartCalc calculator(context, schema, channels);
  int64_t output = 85;
  ASSERT_EQ(common::OB_SUCCESS, calculator.get_previous_row_tablet_id(output));
  EXPECT_EQ(OB_INVALID_INDEX_INT64, output);
  const int64_t inputs[] = {0, 1, 99999, OB_INVALID_INDEX_INT64,
      static_cast<int64_t>(ObExprCalcPartitionId::NONE_PARTITION_ID),
      std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()};
  ObSliceIdxCalc &polymorphic = calculator;
  for (const int64_t input : inputs) {
    calculator.set_cached_tablet(input);
    output = 85;
    ASSERT_EQ(common::OB_SUCCESS, polymorphic.get_previous_row_tablet_id(output));
    EXPECT_EQ(input, output);
    calculator.set_cached_tablet(77);
    EXPECT_EQ(input, output); // copy remains independent of the cached slot
  }
}

} // namespace sql
} // namespace oceanbase
