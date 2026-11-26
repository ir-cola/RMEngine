#pragma once
// アニメーション - ルールの条件の小さな式 (docs/ANIMATION.md 4-8)。純粋 (描画もファイルもない)。
// tests/TestAnimationRules.cpp でテストする。
//
//   名前      パラメーター (数・整数・真偽・トリガー)・t (今の状態の中の割合 0..1)・ended (終わった・1 周した)。
//             名前は日本語でもよい (英字・数字・_ と、ASCII でない文字)
//   数・真偽  0.1・3・true・false
//   比べる    <  <=  >  >=  ==  !=
//   つなぐ    &&  ||  !  ( )
//   計算      + - * /・abs(x)・min(a, b)・max(a, b)。0 で割ると 0
//   空        いつも真
//
// 値はすべて数 (真 = 0 でない。比べる・つなぐの結果は 1 か 0)。強さは C++ と同じ:
// || < && < == != < < <= > >= < + - < * / < ! と単項の - < 呼び出し・( )。

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::animation {

class Condition {
public:
    // 空の式 (いつも真)。
    Condition() = default;
    // `text` を解く。間違い: ok() が false で、error() に理由 (日本語)、column() に何文字目か (1 から)。
    static Condition Parse(std::string_view text);

    bool ok() const { return error_.empty(); }
    bool empty() const { return root_ < 0 && ok(); }
    const std::string& error() const { return error_; }
    size_t column() const { return column_; }
    // 式が使う名前 (出てきた順・重なりなし)。
    const std::vector<std::string>& names() const { return names_; }

    // 値を求める。`lookup`: 名前の値 (知らない名前: nullopt → 0 として計算し、`unknown` に足す)。
    // 間違いのある式: 0 (当たらない)。空の式: 1。
    double Evaluate(const std::function<std::optional<double>(std::string_view)>& lookup, std::vector<std::string>* unknown = nullptr) const;
    bool Test(const std::function<std::optional<double>(std::string_view)>& lookup, std::vector<std::string>* unknown = nullptr) const {
        return Evaluate(lookup, unknown) != 0.0;
    }

    enum class Op { Number, Name, Neg, Not, Add, Sub, Mul, Div, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Abs, Min, Max };
    struct Node {
        Op op = Op::Number;
        double number = 0;  // Number
        int name = -1;      // Name: names_ の番号
        int a = -1, b = -1;  // 子 (nodes_ の番号)
    };

private:
    double Eval(int node, const std::function<std::optional<double>(std::string_view)>& lookup, std::vector<std::string>* unknown) const;

    std::vector<Node> nodes_;
    std::vector<std::string> names_;
    int root_ = -1;
    std::string error_;
    size_t column_ = 0;

    friend class ConditionParser;
};

}  // namespace rm::animation
