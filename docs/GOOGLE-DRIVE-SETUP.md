# Google Drive: what you have to do, and why I cannot

LumenPDF's Google Drive integration is written and ready. It cannot work until
somebody registers an OAuth client with Google, and that somebody has to be you.

## Why this needs you

Google identifies *applications*, not users, with a client ID. Getting one means
agreeing to Google's API terms, naming a legal entity, and — because reading
someone's Drive is sensitive — eventually submitting the app for verification.
All of that is a commitment made by a person or a company. It is not something
that can be done on your behalf, and a client ID belonging to anyone else would
be both against Google's terms and a security problem for your users.

The integration is also deliberately built so the secret never ships inside the
application. See "Why there is no client secret" below.

## What to do

**1. Create a project**

Go to <https://console.cloud.google.com/projectcreate>, name it something like
`LumenPDF`, and create it.

**2. Enable the Drive API**

APIs & Services › Library › search "Google Drive API" › Enable.

**3. Configure the consent screen**

APIs & Services › OAuth consent screen.

- User type: **External** (unless everyone using LumenPDF is in one Workspace).
- App name: `LumenPDF`, your support email, your developer email.
- Scopes: add **`https://www.googleapis.com/auth/drive.file`** and nothing else.

  That scope grants access only to files the user opens with LumenPDF or that
  LumenPDF created. It cannot see the rest of their Drive. The broader
  `drive.readonly` scope would be easier and is the wrong trade: a PDF editor
  has no business reading someone's tax returns, and the narrow scope is also
  what keeps verification simple.

**4. Create the client ID**

APIs & Services › Credentials › Create credentials › OAuth client ID.

- Application type: **Desktop app**
- Name: `LumenPDF Desktop`

Google gives you a client ID and a client secret. Copy both.

**5. Give them to LumenPDF**

Either put them in the environment:

```powershell
setx LUMEN_GOOGLE_CLIENT_ID     "your-id.apps.googleusercontent.com"
setx LUMEN_GOOGLE_CLIENT_SECRET "your-secret"
```

or into `%APPDATA%\Lumen\LumenPDF\google.json`:

```json
{
  "client_id":     "your-id.apps.googleusercontent.com",
  "client_secret": "your-secret"
}
```

Restart LumenPDF. The Drive actions appear in the ⋯ menu once credentials are
present, and stay hidden when they are not — a menu item that always fails is
worse than no menu item.

**6. While testing**

Until the app is verified, add your own Google account under "Test users" on the
consent screen. Without that, sign-in fails with `access_denied`.

## Why there is no client secret in the app

For desktop applications Google's "client secret" is not a secret in any
meaningful sense — anyone can extract it from a binary, and Google's own
documentation says as much. LumenPDF therefore uses **PKCE** (RFC 7636), where
the security comes from a code verifier generated fresh for each sign-in rather
than from a value baked into the executable.

The redirect goes to `http://127.0.0.1:<random port>`, a loopback listener that
exists for the few seconds of the sign-in and is then closed. That is Google's
recommended flow for installed applications, and it avoids the alternative of
embedding a browser and asking users to type their Google password into
something that is not their browser.

## Where the token is kept

The refresh token is stored with the **Windows Data Protection API**, encrypted
against your Windows account, in
`%APPDATA%\Lumen\LumenPDF\google-token.dat`. Another user on the same machine
cannot read it, and copying the file to another machine makes it useless.

To sign out, use the menu — or delete that file, which has exactly the same
effect.

## What LumenPDF can and cannot see

With `drive.file` it can:

- open a PDF you pick through Google's own file picker
- save changes back to a file it opened
- upload a new file it created

It cannot list, search, read or modify anything else in your Drive. That is
enforced by Google, not by LumenPDF promising to behave.
