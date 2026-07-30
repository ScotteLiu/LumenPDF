#include "bridge/FormController.h"

#include "core/PdfDocument.h"

#include <QLoggingCategory>
#include <Qt>

Q_LOGGING_CATEGORY(lcForm, "lumen.form")

namespace lumen {

namespace {

// PDFium's FPDF_FORMFIELD_* values, collapsed to what the UI actually branches
// on. Kept here rather than including PDFium's headers into the bridge layer.
constexpr int kPdfiumUnknown = 0;
constexpr int kPdfiumPushButton = 1;
constexpr int kPdfiumCheckBox = 2;
constexpr int kPdfiumRadioButton = 3;
constexpr int kPdfiumComboBox = 4;
constexpr int kPdfiumListBox = 5;
constexpr int kPdfiumTextField = 6;
constexpr int kPdfiumSignature = 7;

// PDFium's FWL key codes, which are not Qt's. Only the keys that matter inside
// a form field are mapped; anything else is left to the text path.
int toPdfiumKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Backspace: return 8;
    case Qt::Key_Tab:       return 9;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return 13;
    case Qt::Key_Escape:    return 27;
    case Qt::Key_Delete:    return 46;
    case Qt::Key_Left:      return 37;
    case Qt::Key_Up:        return 38;
    case Qt::Key_Right:     return 39;
    case Qt::Key_Down:      return 40;
    case Qt::Key_Home:      return 36;
    case Qt::Key_End:       return 35;
    case Qt::Key_PageUp:    return 33;
    case Qt::Key_PageDown:  return 34;
    default:                return -1;
    }
}

} // namespace

FormController::FormController(QObject *parent)
    : QObject(parent)
{
}

void FormController::setDocument(const QSharedPointer<PdfDocument> &document)
{
    m_document = document;
    setEditingPage(-1);
    emit documentChanged();

    if (hasForms())
        qCInfo(lcForm) << "document has fillable form fields";
}

bool FormController::hasForms() const
{
    return m_document && m_document->isValid() && m_document->hasForms();
}

void FormController::setEditingPage(int pageIndex)
{
    if (m_editingPage == pageIndex)
        return;
    m_editingPage = pageIndex;
    emit editingChanged();
}

int FormController::fieldKindAt(int pageIndex, const QPointF &point) const
{
    if (!hasForms())
        return None;

    switch (m_document->formFieldTypeAt(pageIndex, point)) {
    case kPdfiumTextField:
        return Text;
    case kPdfiumCheckBox:
    case kPdfiumRadioButton:
        return Checkable;
    case kPdfiumComboBox:
    case kPdfiumListBox:
        return Choice;
    case kPdfiumPushButton:
    case kPdfiumSignature:
        return Other;
    case kPdfiumUnknown:
    default:
        return None;
    }
}

bool FormController::press(int pageIndex, const QPointF &point, int modifiers)
{
    if (!hasForms())
        return false;

    // Ask first, then deliver. Handing PDFium a click that missed every field
    // would silently steal it from text selection.
    if (fieldKindAt(pageIndex, point) == None) {
        if (isEditing()) {
            // Clicking away commits whatever was being typed.
            m_document->formClearFocus();
            emit pageInvalidated(m_editingPage);
            setEditingPage(-1);
        }
        return false;
    }

    const bool consumed = m_document->formMousePress(pageIndex, point, modifiers);
    setEditingPage(pageIndex);
    emit pageInvalidated(pageIndex);
    return consumed || true;   // the click belonged to the field either way
}

bool FormController::release(int pageIndex, const QPointF &point, int modifiers)
{
    if (!hasForms() || !isEditing())
        return false;

    const bool consumed = m_document->formMouseRelease(pageIndex, point, modifiers);

    // A checkbox toggles on release, so this is where the repaint is needed.
    emit pageInvalidated(pageIndex);
    return consumed;
}

bool FormController::move(int pageIndex, const QPointF &point, int modifiers)
{
    if (!hasForms() || !isEditing())
        return false;
    return m_document->formMouseMove(pageIndex, point, modifiers);
}

bool FormController::key(int qtKey, int modifiers)
{
    if (!hasForms() || !isEditing())
        return false;

    const int pdfiumKey = toPdfiumKey(qtKey);
    if (pdfiumKey < 0)
        return false;

    // Escape leaves the field rather than being delivered to it.
    if (qtKey == Qt::Key_Escape) {
        m_document->formClearFocus();
        emit pageInvalidated(m_editingPage);
        setEditingPage(-1);
        return true;
    }

    const bool consumed = m_document->formKeyPress(m_editingPage, pdfiumKey, modifiers);
    if (consumed)
        emit pageInvalidated(m_editingPage);
    return consumed;
}

bool FormController::text(const QString &input)
{
    if (!hasForms() || !isEditing() || input.isEmpty())
        return false;

    const bool consumed = m_document->formTextInput(m_editingPage, input);
    if (consumed)
        emit pageInvalidated(m_editingPage);
    return consumed;
}

void FormController::clearFocus()
{
    if (!hasForms() || !isEditing())
        return;

    m_document->formClearFocus();
    emit pageInvalidated(m_editingPage);
    setEditingPage(-1);
}

} // namespace lumen
