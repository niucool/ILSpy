// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The `PatternExtensions::ToType` / `ToExpression` / `ToStatement` / `WithName`
// definitions (declared in PatternMatching/PatternNodes.hpp). They are out-of-line
// because each constructs a `PatternPlaceholderNode<Base>` (PatternPlaceholder.hpp),
// whose base must be complete; keeping the definitions here avoids pulling every AST
// base into the widely included PatternNodes.hpp and sidesteps the include cycle
// (the placeholder template derives from the base, so the base header cannot include
// PatternNodes.hpp).

#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

AstType* PatternExtensions::ToType(std::shared_ptr<Pattern> pattern) {
    if (pattern == nullptr)
        return nullptr;
    return new PatternPlaceholderNode<AstType>(std::move(pattern));
}

Expression* PatternExtensions::ToExpression(std::shared_ptr<Pattern> pattern) {
    if (pattern == nullptr)
        return nullptr;
    return new PatternPlaceholderNode<Expression>(std::move(pattern));
}

Statement* PatternExtensions::ToStatement(std::shared_ptr<Pattern> pattern) {
    if (pattern == nullptr)
        return nullptr;
    return new PatternPlaceholderNode<Statement>(std::move(pattern));
}

Expression* PatternExtensions::WithName(Expression& node, const std::string& patternGroupName) {
    return ToExpression(std::make_shared<NamedNode>(patternGroupName, &node));
}

Statement* PatternExtensions::WithName(Statement& node, const std::string& patternGroupName) {
    return ToStatement(std::make_shared<NamedNode>(patternGroupName, &node));
}

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
