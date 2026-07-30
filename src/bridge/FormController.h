#pragma once

#include <QObject>
#include <QPointF>
#include <QSharedPointer>

namespace lumen {

class PdfDocument;

// AcroForm filling.
//
// PDFium owns the field behaviour -- caret, selection, validation, checkbox
// toggling -- so this class is a router, not a text editor. Its job is to
// decide whether a pointer event belongs to a form field or to text selection,
// forward what belongs to PDFium, and tell the view to redraw when PDFium says
// it changed something.
class FormController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool hasForms READ hasForms NOTIFY documentChanged)
    Q_PROPERTY(bool editing READ isEditing NOTIFY editingChanged)
    Q_PROPERTY(int editingPage READ editingPage NOTIFY editingChanged)

public:
    // Mirrors PDFium's FPDF_FORMFIELD_* values closely enough for the UI to
    // pick a cursor and decide whether typing is meaningful.
    enum FieldKind {
        None = 0,
        Text,
        Checkable,   // checkbox or radio button
        Choice,      // combo or list box
        Other,
    };
    Q_ENUM(FieldKind)

    explicit FormController(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool hasForms() const;
    bool isEditing() const { return m_editingPage >= 0; }
    int editingPage() const { return m_editingPage; }

    // What kind of field, if any, sits under a page-local point in PDF points.
    Q_INVOKABLE int fieldKindAt(int pageIndex, const QPointF &point) const;

    // Pointer events. press() returns true when the event landed on a field,
    // which is the view's signal to leave text selection alone.
    Q_INVOKABLE bool press(int pageIndex, const QPointF &point, int modifiers);
    Q_INVOKABLE bool release(int pageIndex, const QPointF &point, int modifiers);
    Q_INVOKABLE bool move(int pageIndex, const QPointF &point, int modifiers);

    // Keyboard. `key` takes a Qt key code and translates it; `text` is what the
    // user actually typed.
    Q_INVOKABLE bool key(int qtKey, int modifiers);
    Q_INVOKABLE bool text(const QString &text);

    Q_INVOKABLE void clearFocus();

signals:
    void documentChanged();
    void editingChanged();

    // PDFium changed how a page looks and it must be re-rendered.
    void pageInvalidated(int pageIndex);

private:
    void setEditingPage(int pageIndex);

    QSharedPointer<PdfDocument> m_document;

    // Which page currently owns form focus, or -1. Keystrokes have to be
    // delivered against a page, and PDFium tracks focus per environment rather
    // than telling us where it is.
    int m_editingPage = -1;
};

} // namespace lumen
