# Information Security: Authentication vs. Authorization

## The Fundamental Distinction in Access Control
Authentication and authorization are the two distinct pillars of digital security. While often conflated, they solve fundamentally different problems in system security.

### 1. Authentication (AuthN): "Who Are You?"
Authentication is the process of verifying the identity of a user, service, or device attempting to access a system.
- **Goal**: Establish proof of identity.
- **Factors of Authentication**:
  1. *Knowledge factors*: Passwords, PINs, security questions ("something you know").
  2. *Possession factors*: Hardware security keys (YubiKey), TOTP authenticator apps, smartcards ("something you have").
  3. *Inherence factors*: Biometrics, fingerprint, facial recognition, retina scans ("something you are").
- **Protocols & Standards**: OpenID Connect (OIDC), SAML 2.0, Kerberos, WebAuthn, FIDO2.

### 2. Authorization (AuthZ): "What Are You Allowed to Do?"
Authorization determines whether an authenticated entity has permission to perform a specific action on a specific resource.
- **Goal**: Grant or deny access rights and capabilities.
- **Mechanisms**:
  - *Role-Based Access Control (RBAC)*: Permissions are assigned to roles (Admin, Editor, Viewer), and users are assigned to roles.
  - *Attribute-Based Access Control (ABAC)*: Decisions are based on user attributes, resource sensitivity, and environmental context (e.g. IP address, time of day).
  - *Access Control Lists (ACLs)*: Per-resource lists mapping identities to permissions (read, write, execute).
  - *Capability Tokens & Scopes*: OAuth 2.0 access tokens, JSON Web Tokens (JWT) containing granular permission claims.

### Summary Comparison Table

| Attribute | Authentication (AuthN) | Authorization (AuthZ) |
|---|---|---|
| Question answered | Who is the user? | What can the user access? |
| Sequence | Happens first | Happens after authentication |
| Failure outcome | 401 Unauthorized (Unauthenticated) | 403 Forbidden (Insufficient permissions) |
| Key mechanisms | Passwords, MFA, biometric, SSO | Roles, permissions, scopes, policies |
