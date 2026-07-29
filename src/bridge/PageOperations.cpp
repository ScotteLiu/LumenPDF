#include "bridge/PageOperations.h"

#include "core/PdfDocument.h"

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

bool PageOperations::undo()
{
    if (!canUndo())
        return false;

    const Command command = m_commands.at(m_position - 1);
    if (!revert(command))
        return false;

    --m_position;
    emit stackChanged();
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
    return true;
}

} // namespace lumen
