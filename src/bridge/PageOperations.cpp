#include "bridge/PageOperations.h"

#include "core/PdfDocument.h"

#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPageOps, "lumen.pageops")

namespace lumen {

PageOperations::PageOperations(QObject *parent)
    : QObject(parent)
{
}

void PageOperations::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    clearHistory();
}

void PageOperations::clearHistory()
{
    m_commands.clear();
    m_position = 0;
    // The stash only makes sense against the history that references it.
    m_stash.reset();
    emit stackChanged();
}

QString PageOperations::undoLabel() const
{
    if (!canUndo())
        return {};

    switch (m_commands.at(m_position - 1).kind) {
    case Command::Rotate: return tr("Undo Rotate Page");
    case Command::Move:   return tr("Undo Move Page");
    case Command::Delete: return tr("Undo Delete Page");
    case Command::Insert: return tr("Undo Insert Pages");
    }
    return tr("Undo");
}

QString PageOperations::redoLabel() const
{
    if (!canRedo())
        return {};

    switch (m_commands.at(m_position).kind) {
    case Command::Rotate: return tr("Redo Rotate Page");
    case Command::Move:   return tr("Redo Move Page");
    case Command::Delete: return tr("Redo Delete Page");
    case Command::Insert: return tr("Redo Insert Pages");
    }
    return tr("Redo");
}

void PageOperations::push(const Command &command)
{
    // A new action after undoing discards the redo branch, as everywhere else.
    if (m_position < m_commands.size())
        m_commands.resize(m_position);

    m_commands.append(command);
    ++m_position;
    emit stackChanged();
}

bool PageOperations::apply(Command &command)
{
    if (!m_document || !m_document->isValid())
        return false;

    switch (command.kind) {
    case Command::Rotate:
        if (!m_document->rotatePage(command.a, command.b))
            return false;
        // Rotation swaps the page's effective width and height, so layout has
        // to be redone -- this is structural, not just a repaint.
        emit structureChanged();
        return true;

    case Command::Move:
        if (!m_document->movePage(command.a, command.b))
            return false;
        emit structureChanged();
        return true;

    case Command::Delete: {
        if (!m_stash)
            m_stash = PdfDocument::createScratch();

        int stashIndex = -1;
        if (!m_document->deletePage(command.a, m_stash.data(), &stashIndex))
            return false;

        // Written back so undo knows where the page went. The stash is never
        // compacted -- redoing a delete stashes a fresh copy -- but it is
        // bounded by how many pages the user actually deleted.
        command.b = stashIndex;
        emit structureChanged();
        return true;
    }

    case Command::Insert: {
        if (!command.source)
            return false;

        const int inserted = m_document->insertPagesFrom(*command.source, {}, command.a);
        if (inserted <= 0)
            return false;

        command.b = inserted;
        emit structureChanged();
        return true;
    }
    }

    return false;
}

bool PageOperations::revert(const Command &command)
{
    if (!m_document || !m_document->isValid())
        return false;

    switch (command.kind) {
    case Command::Rotate:
        if (!m_document->rotatePage(command.a, -command.b))
            return false;
        emit structureChanged();
        return true;

    case Command::Move:
        // The page now sits at `to`; put it back where it came from.
        if (!m_document->movePage(command.b, command.a))
            return false;
        emit structureChanged();
        return true;

    case Command::Delete:
        if (!m_stash || command.b < 0)
            return false;
        if (!m_document->insertPageFrom(*m_stash, command.b, command.a))
            return false;
        emit structureChanged();
        return true;

    case Command::Insert:
        // No stash needed: redo can re-import from the source document, which
        // the command keeps alive precisely for that reason.
        if (command.b <= 0)
            return false;
        if (!m_document->deletePageRange(command.a, command.b))
            return false;
        emit structureChanged();
        return true;
    }

    return false;
}

bool PageOperations::rotate(int pageIndex, int quarterTurns)
{
    Command command { Command::Rotate, pageIndex, quarterTurns };
    if (!apply(command))
        return false;
    push(command);
    qCInfo(lcPageOps) << "rotate page" << pageIndex << "by" << quarterTurns * 90;
    return true;
}

bool PageOperations::move(int from, int to)
{
    Command command { Command::Move, from, to };
    if (!apply(command))
        return false;
    push(command);
    qCInfo(lcPageOps) << "move page" << from << "->" << to;
    return true;
}

bool PageOperations::remove(int pageIndex)
{
    Command command { Command::Delete, pageIndex, -1 };
    if (!apply(command))
        return false;
    push(command);
    qCInfo(lcPageOps) << "delete page" << pageIndex << "stashed at" << command.b;
    return true;
}

bool PageOperations::mergeFrom(const QUrl &url)
{
    if (!m_document || !m_document->isValid())
        return false;
    return insertFrom(url, m_document->pageCount());
}

bool PageOperations::insertFrom(const QUrl &url, int atIndex)
{
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return false;
    }

    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        emit failed(tr("No file given."));
        return false;
    }

    if (QFileInfo(path) == QFileInfo(m_document->filePath())) {
        // Importing a document into itself would work, but almost nobody means
        // it, and the result is a silently doubled file.
        emit failed(tr("That is the document already open."));
        return false;
    }

    auto source = QSharedPointer<PdfDocument>::create();
    if (!source->load(path)) {
        emit failed(source->lastError().isEmpty()
                    ? tr("Could not open %1").arg(QFileInfo(path).fileName())
                    : source->lastError());
        return false;
    }

    Command command;
    command.kind = Command::Insert;
    command.a = qBound(0, atIndex, m_document->pageCount());
    command.b = 0;
    command.source = source;

    if (!apply(command)) {
        emit failed(tr("Could not insert pages from %1").arg(QFileInfo(path).fileName()));
        return false;
    }

    push(command);
    qCInfo(lcPageOps) << "inserted" << command.b << "pages from" << path
                      << "at" << command.a;
    emit merged(path, command.b);
    return true;
}

bool PageOperations::extractTo(const QUrl &url, int firstPage, int lastPage)
{
    if (!m_document || !m_document->isValid()) {
        emit failed(tr("No document is open."));
        return false;
    }

    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        emit failed(tr("No destination given."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");

    const int total = m_document->pageCount();
    const int from = qBound(0, firstPage, total - 1);
    const int to = qBound(from, lastPage, total - 1);

    // PDFium's range syntax is 1-based and inclusive.
    const QString range = (from == to)
        ? QString::number(from + 1)
        : QStringLiteral("%1-%2").arg(from + 1).arg(to + 1);

    if (!m_document->extractPagesTo(path, range)) {
        emit failed(tr("Could not write %1").arg(QFileInfo(path).fileName()));
        return false;
    }

    const int count = to - from + 1;
    qCInfo(lcPageOps) << "extracted pages" << range << "to" << path;
    emit extracted(path, count);
    return true;
}

bool PageOperations::undo()
{
    if (!canUndo())
        return false;

    const Command command = m_commands.at(m_position - 1);
    if (!revert(command))
        return false;

    --m_position;
    emit stackChanged();

    qCInfo(lcPageOps) << "undo" << int(command.kind)
                      << "-> pageCount" << m_document->pageCount();
    return true;
}

bool PageOperations::redo()
{
    if (!canRedo())
        return false;

    Command command = m_commands.at(m_position);
    if (!apply(command))
        return false;

    m_commands[m_position] = command;   // Delete rewrites its stash index
    ++m_position;
    emit stackChanged();

    qCInfo(lcPageOps) << "redo" << int(command.kind)
                      << "-> pageCount" << m_document->pageCount();
    return true;
}

} // namespace lumen
