#pragma once

#include <QObject>
#include <QSharedPointer>

namespace lumen {

class PdfDocument;
class SelectionController;

// Redaction: permanently removing content, not covering it.
//
// Kept separate from AnnotationController on purpose. A highlight is
// decoration and is undoable; a redaction destroys page objects and cannot be
// undone once saved. Sharing a class would invite sharing a code path, and
// these two must never be confused.
class RedactionController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool canRedact READ canRedact NOTIFY canRedactChanged)

public:
    explicit RedactionController(SelectionController *selection,
                                 QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool canRedact() const;

    // Redacts the current text selection on every page it touches.
    Q_INVOKABLE bool redactSelection();

signals:
    void canRedactChanged();
    void pageInvalidated(int pageIndex);

    // Reports what was actually destroyed, which can exceed what was selected.
    void redacted(int textObjects, int imageObjects);

    // Redaction flattens each affected page to an image. The user has to be
    // told: those pages are no longer searchable or selectable.
    void flattenedPages(int pageCount);

    void failed(const QString &reason);

private:
    QSharedPointer<PdfDocument> m_document;
    SelectionController *m_selection = nullptr;
};

} // namespace lumen
