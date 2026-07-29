#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QVector>

namespace lumen {

class PdfDocument;

// Page-level editing -- rotate, reorder, delete -- with undo and redo.
//
// PDFium has no undo of its own, so this keeps a command stack and inverts
// each operation. Rotation and reordering invert arithmetically. Deletion
// cannot, so a deleted page is first copied into a scratch document that
// exists only to hold it; undo imports it back. The scratch document *is* the
// undo buffer, and it keeps the page's annotations with it.
class PageOperations : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY stackChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY stackChanged)

public:
    explicit PageOperations(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position < m_commands.size(); }
    QString undoLabel() const;
    QString redoLabel() const;

    Q_INVOKABLE bool rotate(int pageIndex, int quarterTurns);
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE bool remove(int pageIndex);

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    Q_INVOKABLE void clearHistory();

signals:
    void stackChanged();

    // The page count or ordering changed; every model over this document has
    // to be reset, and every cached raster is stale.
    void structureChanged();

    // Only the given page's appearance changed.
    void pageInvalidated(int pageIndex);

private:
    struct Command {
        enum Kind { Rotate, Move, Delete } kind;
        int a = 0;          // rotate: page   move: from   delete: page
        int b = 0;          // rotate: turns  move: to     delete: stash index
    };

    // Takes a mutable reference: deleting writes back where the page was
    // stashed, which undo needs and the caller stores.
    bool apply(Command &command);
    bool revert(const Command &command);
    void push(const Command &command);

    QSharedPointer<PdfDocument> m_document;

    // Holds deleted pages so they can come back. Created lazily -- most
    // sessions never delete a page and should not pay for an extra document.
    QSharedPointer<PdfDocument> m_stash;

    QVector<Command> m_commands;

    // Index of the next command to redo; everything before it is applied.
    int m_position = 0;
};

} // namespace lumen
