#pragma once
#include <QStringList>
#include <QList>
#include <QHash>
#include <QRegularExpression>

// Deliberately narrow aliases: related topics are not necessarily synonyms.
namespace LocalSearch {
struct Group { QString original; QStringList alternatives; };
struct Token { QString original; QList<Group> groups; bool expanded = false; };
inline const QHash<QString, QStringList>& aliases() {
    static const auto table = [] {
        QHash<QString, QStringList> result;
        const QList<QStringList> groups{
            {"游戏", "game", "games", "gaming"},
            {"攻略", "walkthrough", "walkthroughs"},
            {"学习", "study", "studies", "studying"},
            {"课程", "course", "courses"}, {"作业", "homework"},
            {"考试", "exam", "exams"}, {"笔记", "note", "notes", "notebook", "notebooks"},
            {"工作", "work"}, {"项目", "project", "projects"},
            {"会议", "meeting", "meetings"}, {"报告", "report", "reports"},
            {"合同", "contract", "contracts"}, {"生活", "life"},
            {"旅行", "旅游", "travel", "travels", "trip", "trips"},
            {"购物", "shopping"}, {"日记", "diary", "diaries"},
            {"代码", "code"}, {"编程", "programming"}, {"软件", "software"},
            {"音乐", "music"}, {"歌曲", "song", "songs"}, {"音频", "audio"},
            {"照片", "photo", "photos", "photograph", "photographs"},
            {"图片", "picture", "pictures", "image", "images"},
            {"摄影", "photography"}, {"相册", "album", "albums"},
            {"视频", "video", "videos"}, {"电影", "movie", "movies", "film", "films"},
            {"财务", "finance", "financial"}, {"发票", "invoice", "invoices"},
            {"账单", "bill", "bills"}, {"预算", "budget", "budgets"}
        };
        for (const auto& group : groups) for (const auto& word : group) result.insert(word, group);
        return result;
    }();
    return table;
}
inline QList<Token> parse(const QString& query) {
    QList<Token> tokens;
    for (const auto& original : query.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        Token token; token.original = original;
        const auto folded = original.toCaseFolded();
        if (aliases().contains(folded)) {
            token.groups.append({original, aliases().value(folded)}); token.expanded = true;
        } else {
            // Segment known Chinese compounds, preserving every unknown character literally.
            QString literal;
            for (qsizetype pos = 0; pos < folded.size();) {
                QString longest;
                for (auto it = aliases().cbegin(); it != aliases().cend(); ++it) {
                    const auto& word = it.key();
                    if (word.front().script() == QChar::Script_Han && word.size() > longest.size()
                        && folded.mid(pos, word.size()) == word) longest = word;
                }
                if (longest.isEmpty()) { literal += original[pos++]; continue; }
                if (!literal.isEmpty()) { token.groups.append({literal, {literal}}); literal.clear(); }
                token.groups.append({longest, aliases().value(longest)}); token.expanded = true;
                pos += longest.size();
            }
            if (!literal.isEmpty()) token.groups.append({literal, {literal}});
        }
        tokens.append(token);
    }
    return tokens;
}
inline bool containsAlternative(const QString& text, const QString& term) {
    static const QRegularExpression english("^[a-z]+$");
    if (!english.match(term).hasMatch()) return text.contains(term, Qt::CaseInsensitive);
    // Expanded English aliases must be words, not fragments of e.g. 'reporter'.
    return QRegularExpression("(?<![A-Za-z])" + QRegularExpression::escape(term) + "(?![A-Za-z])",
                              QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
struct Match { int score = 0; bool expanded = false; QStringList reasons; };
inline Match match(const QList<Token>& tokens, const QString& name, const QString& body) {
    Match result; result.score = 1;
    for (const auto& token : tokens) {
        if (name.contains(token.original, Qt::CaseInsensitive)) {
            result.score += 1000; result.reasons.append("名称直接匹配：" + token.original); continue;
        }
        if (body.contains(token.original, Qt::CaseInsensitive)) {
            result.score += 100; result.reasons.append("正文直接匹配：" + token.original); continue;
        }
        if (!token.expanded) return {};
        bool allNames = true; QStringList reasons;
        for (const auto& group : token.groups) {
            QString found; bool inName = false;
            for (const auto& alternative : group.alternatives) {
                if (containsAlternative(name, alternative)) { found = alternative; inName = true; break; }
            }
            if (found.isEmpty()) for (const auto& alternative : group.alternatives) {
                if (containsAlternative(body, alternative)) { found = alternative; break; }
            }
            if (found.isEmpty()) return {};
            allNames &= inName;
            reasons.append((inName ? QString("名称扩展匹配：") : QString("正文扩展匹配：")) + group.original + " → " + found);
        }
        result.score += allNames ? 10 : 1;
        result.expanded = true; result.reasons.append(reasons);
    }
    return result;
}
} // namespace LocalSearch
