This is a [Next.js](https://nextjs.org) project bootstrapped with [`create-next-app`](https://nextjs.org/docs/app/api-reference/cli/create-next-app).

## Getting Started

First, run the development server:

```bash
npm run dev
# or
yarn dev
# or
pnpm dev
# or
bun dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

## Backend Configuration

The client does not need the backend running locally if you point the local
Next.js server at a remote Loomic backend.

1. Create `client/.env.local`.
2. Set `LOOMIC_API_BASE_URL` to the backend you want to use.

Example:

```bash
LOOMIC_API_BASE_URL=http://35.232.85.186:8080
```

Then restart `npm run dev`. The Next.js route handlers under
`src/app/api/auth/*` will proxy requests to that backend even during local
development.

For deployed HTTPS environments, the browser WebSocket URL must also be secure.
If your backend supports TLS for the `/ws` endpoint, set:

```bash
LOOMIC_WS_URL=wss://your-backend-host:8080/ws
```

If `LOOMIC_WS_URL` is not set, the client derives the socket endpoint from
`LOOMIC_API_BASE_URL` and will prefer `wss://` when the app itself is served
over HTTPS.

## Live Chat Bridge

The `/chat` page now uses a browser-safe bridge:

1. The browser talks to Next.js route handlers under `src/app/api/chat/*`.
2. The Next.js server opens the secure Loomic TCP session on port `9000`.
3. Messages stream back to the browser over Server-Sent Events.

Optional `.env.local` overrides:

```bash
LOOMIC_API_BASE_URL=http://35.232.85.186:8080
LOOMIC_WS_URL=wss://your-backend-host:8080/ws
LOOMIC_TCP_HOST=35.232.85.186
LOOMIC_TCP_PORT=9000
LOOMIC_TCP_SERVERNAME=35.232.85.186
LOOMIC_TCP_VERIFY_TLS=false
```

The current frontend keeps recent live messages in session memory because the
backend does not yet expose an HTTP history endpoint.

You can start editing the page by modifying `app/page.tsx`. The page auto-updates as you edit the file.

This project uses [`next/font`](https://nextjs.org/docs/app/building-your-application/optimizing/fonts) to automatically optimize and load [Geist](https://vercel.com/font), a new font family for Vercel.

## Learn More

To learn more about Next.js, take a look at the following resources:

- [Next.js Documentation](https://nextjs.org/docs) - learn about Next.js features and API.
- [Learn Next.js](https://nextjs.org/learn) - an interactive Next.js tutorial.

You can check out [the Next.js GitHub repository](https://github.com/vercel/next.js) - your feedback and contributions are welcome!

## Deploy on Vercel

The easiest way to deploy your Next.js app is to use the [Vercel Platform](https://vercel.com/new?utm_medium=default-template&filter=next.js&utm_source=create-next-app&utm_campaign=create-next-app-readme) from the creators of Next.js.

Check out our [Next.js deployment documentation](https://nextjs.org/docs/app/building-your-application/deploying) for more details.
